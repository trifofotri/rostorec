#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

#include <curl/curl.h>
#include <cJSON.h>
#include <rbxm.h>
#include <fetch.h>
#include <string.h>
#include <string.h>

#define PART_SIZE (100u * 1024 * 1024)

typedef struct cfg_ctx {
    uint64_t universeId;
    uint64_t placeId;
    const char* apiKey;
    const char* inputPath;
} cfg_ctx;

static cfg_ctx cfg;

uint8_t *read_file(const char *path, uint32_t *out_len) {
    FILE *input = fopen(path, "rb");
    if (!input) {
        return NULL;
    }

    fseek(input, 0, SEEK_END);
    long size = ftell(input);
    fseek(input, 0, SEEK_SET);

    if (size < 0 || (unsigned long) size > UINT32_MAX) {
        fclose(input);
        return NULL;
    }

    uint8_t *buffer = malloc(size);
    if (!buffer) {
        fclose(input);
        return NULL;
    }

    if (fread(buffer, 1, size, input) != (size_t) size) {
        free(buffer);
        fclose(input);
        return NULL;
    }

    fclose(input);
    *out_len = (uint32_t)size;
    return buffer;
}

int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *output = fopen(path, "wb");
    if (!output) return 0;

    size_t written = fwrite(data, 1, size, output);
    fclose(output);

    return ( written == size );
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
    return slash ? slash + 1 : path;
}

static cJSON *load_manifest(const char *path) {
    uint32_t length;
    uint8_t *text = read_file(path, &length);
    if (!text) return NULL;

    cJSON *root = cJSON_ParseWithLength((const char *)text, length);
    free(text);
    return root;
}

static int save_manifest(const char *path, cJSON *root) {
    char *text = cJSON_Print(root);
    if (!text) return 0;

    int ok = write_file(path, (const uint8_t *)text, strlen(text));
    cJSON_free(text);
    return ok;
}

static int put_command(const char *path, const char *manifest_path) {
    int result = 1;
    FILE *input = NULL;
    uint8_t *buffer = NULL;
    cJSON *parts = NULL;
    cJSON *manifest = NULL;
    cJSON *files = NULL;
    cJSON *entry = NULL;
    long total = 0;
    int part_count = 0;

    input = fopen(path, "rb");
    if (!input) {
        fprintf(stderr, "could not open %s\n", path);
        goto cleanup;
    }

    fseek(input, 0, SEEK_END);
    total = ftell(input);
    fseek(input, 0, SEEK_SET);
    if (total <= 0) {
        fprintf(stderr, "file is empty\n");
        goto cleanup;
    }

    part_count = (int) ((total + PART_SIZE - 1) / PART_SIZE);
    buffer = malloc(PART_SIZE);
    parts = cJSON_CreateArray();
    if (!buffer || !parts) {
        fprintf(stderr, "out of memory\n");
        goto cleanup;
    }

    for (int i = 0; i < part_count; i++) {
        size_t got = fread(buffer, 1, PART_SIZE, input);
        if (got == 0) {
            fprintf(stderr, "read error\n");
            goto cleanup;
        }

        size_t place_size = 0;
        uint8_t *place = build_place(buffer, (uint32_t)got, &place_size);
        if (!place) {
            fprintf(stderr, "out of memory\n");
            goto cleanup;
        }

        char label[64];
        snprintf(label, sizeof label, "%d/%d", i + 1, part_count);
        int version = upload_place(cfg.apiKey, cfg.universeId, cfg.placeId, place, place_size, label);
        free(place);

        if (version < 0) {
            fprintf(stderr, "part %d failed. versions uploaded before it:", i + 1);
            cJSON *done;
            cJSON_ArrayForEach(done, parts) {
                fprintf(stderr, " v%d", cJSON_GetObjectItemCaseSensitive(done, "version")->valueint);
            }
            fprintf(stderr, "\n");
            goto cleanup;
        }

        cJSON *part = cJSON_CreateObject();
        cJSON_AddNumberToObject(part, "version", version);
        cJSON_AddNumberToObject(part, "size", (double)got);
        cJSON_AddItemToArray(parts, part);
        printf("part %d/%d -> version %d\n", i + 1, part_count, version);
    }

    manifest = load_manifest(manifest_path);
    if (!manifest) manifest = cJSON_CreateObject();

    files = cJSON_GetObjectItemCaseSensitive(manifest, "files");
    if (!cJSON_IsObject(files)) {
        cJSON_DeleteItemFromObjectCaseSensitive(manifest, "files");
        files = cJSON_AddObjectToObject(manifest, "files");
    }

    const char *name = base_name(path);
    cJSON_DeleteItemFromObjectCaseSensitive(files, name);

    entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "total_size", (double)total);
    cJSON_AddNumberToObject(entry, "universe_id", (double)cfg.universeId);
    cJSON_AddNumberToObject(entry, "place_id", (double)cfg.placeId);
    cJSON_AddItemToObject(entry, "parts", parts);
    parts = NULL;
    cJSON_AddItemToObject(files, name, entry);

    if (!save_manifest(manifest_path, manifest)) {
        fprintf(stderr, "could not write %s\n", manifest_path);
        goto cleanup;
    }
    
    printf("saved \"%s\" in %s\n", name, manifest_path);
    result = 0;

cleanup:
    if (input) {
        fclose(input);
    }

    free(buffer);
    cJSON_Delete(parts);
    cJSON_Delete(manifest);
    return result;
}

static int get_command(const char *manifest_path, const char *name, const char *out_path) {
    int result = 1;
    FILE *output = NULL;
    cJSON *manifest = load_manifest(manifest_path);
    if (!manifest) {
        fprintf(stderr, "could not read %s\n", manifest_path);
        return 1;
    }

    cJSON *files = cJSON_GetObjectItemCaseSensitive(manifest, "files");
    cJSON *entry = cJSON_GetObjectItemCaseSensitive(files, name);
    if (!cJSON_IsObject(entry)) {
        fprintf(stderr, "\"%s\" is not in %s. available files:\n", name, manifest_path);
        cJSON *each;
        cJSON_ArrayForEach(each, files) fprintf(stderr, "  %s\n", each->string);
        goto cleanup;
    }

    cJSON *place_item = cJSON_GetObjectItemCaseSensitive(entry, "place_id");
    uint64_t place_id = cJSON_IsNumber(place_item) ? (uint64_t)place_item->valuedouble : cfg.placeId;

    cJSON *parts = cJSON_GetObjectItemCaseSensitive(entry, "parts");
    if (!cJSON_IsArray(parts)) {
        fprintf(stderr, "entry for \"%s\" has no parts\n", name);
        goto cleanup;
    }

    output = fopen(out_path, "wb");
    if (!output) {
        fprintf(stderr, "could not write %s\n", out_path);
        goto cleanup;
    }

    int part_count = cJSON_GetArraySize(parts);
    int index = 0;
    cJSON *part;
    cJSON_ArrayForEach(part, parts) {
        index++;
        int version = cJSON_GetObjectItemCaseSensitive(part, "version")->valueint;
        size_t expected = (size_t)cJSON_GetObjectItemCaseSensitive(part, "size")->valuedouble;
        printf("part %d/%d: version %d\n", index, part_count, version);

        size_t place_size;
        uint8_t *place = download_place(cfg.apiKey, place_id, version, &place_size);
        if (!place) {
            fprintf(stderr, "download of version %d failed\n", version);
            goto cleanup;
        }

        uint32_t stored_len;
        uint8_t *stored = extract_value(place, place_size, &stored_len);
        free(place);
        if (!stored || stored_len != expected) {
            fprintf(stderr, "version %d: wrong data (got %u bytes, expected %zu)\n", version, stored ? stored_len : 0, expected);
            free(stored);
            goto cleanup;
        }

        size_t written = fwrite(stored, 1, stored_len, output);
        free(stored);
        if (written != stored_len) {
            fprintf(stderr, "write error on %s\n", out_path);
            goto cleanup;
        }
    }

    printf("wrote %s\n", out_path);
    result = 0;

cleanup:
    if (output) {
        fclose(output);
        if (result != 0) remove(out_path);
    }
    cJSON_Delete(manifest);
    return result;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr,
            "Usage: %s <api-key> <universe-id> <place-id> put <file> <manifest.json>\n"
            "       %s <api-key> <universe-id> <place-id> get <manifest.json> <file-name> <out-path>\n",
            argv[0], argv[0]);
        return 1;
    }

    cfg.apiKey = argv[1];
    cfg.universeId = strtoull(argv[2], NULL, 10);
    cfg.placeId = strtoull(argv[3], NULL, 10);
    const char *command = argv[4];

    if (strcmp(command, "put") == 0) {
        if (argc < 7) {
            fprintf(stderr, "put needs <file> <manifest.json>\n");
            return 1;
        }
        return put_command(argv[5], argv[6]);
    }

    if (strcmp(command, "get") == 0) {
        if (argc < 8) {
            fprintf(stderr, "get needs <manifest.json> <file-name> <out-path>\n");
            return 1;
        }
        return get_command(argv[5], argv[6], argv[7]);
    }

    fprintf(stderr, "unknown command: %s\n", command);
    return 1;
}
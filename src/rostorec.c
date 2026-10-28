#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

#include <curl/curl.h>
#include <rbxm.h>
#include <fetch.h>
#include <string.h>

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

//  1 on success, 0 on failure
int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *output = fopen(path, "wb");
    if (!output) return 0;

    size_t written = fwrite(data, 1, size, output);
    fclose(output);

    return ( written == size );
}

static int upload_place(const char *api_key, uint64_t universe_id, uint64_t place_id, const uint8_t *file, size_t file_size) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    /* https://create.roblox.com/docs/cloud/guides/usage-place-publishing */
    char url[256];
    snprintf(url, sizeof url, "https://apis.roblox.com/universes/v1/%" PRIu64 "/places/%" PRIu64 "/versions?versionType=Saved", universe_id, place_id);

    char key_header[1024];
    snprintf(key_header, sizeof key_header, "x-api-key: %s", api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, key_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)file_size);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, file);

    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        fprintf(stderr, "curl failed: %s\n", curl_easy_strerror(result));
        return 0;
    }
    if (status != 200) {
        fprintf(stderr, "upload failed, HTTP %ld\n", status);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <api-key> <universe-id> <place-id> put <file>\n"
                        "       %s <api-key> <universe-id> <place-id> get <out-file>\n", argv[0], argv[0]);
        return 1;
    }
    cfg.apiKey = argv[1];
    cfg.universeId = strtoull(argv[2], NULL, 10);
    cfg.placeId = strtoull(argv[3], NULL, 10);
    const char *command = argv[4];
    const char *path = argv[5];

    if (strcmp(command, "put") == 0) {
        uint32_t data_len;
        uint8_t *data = read_file(path, &data_len);
        if (!data) {
            fprintf(stderr, "could not read %s\n", path);
            return 1;
        }

        size_t file_size;
        uint8_t *file = build_place(data, data_len, &file_size);
        free(data);
        if (!file) return 1;

        int ok = upload_place(cfg.apiKey, cfg.universeId, cfg.placeId, file, file_size);
        free(file);
        printf("\n");
        return ok ? 0 : 1;
    }

    if (strcmp(command, "get") == 0) {
        size_t place_size;
        uint8_t *place = download_place(cfg.apiKey, cfg.placeId, 0, &place_size);
        if (!place) return 1;

        uint32_t stored_len;
        uint8_t *stored = extract_value(place, place_size, &stored_len);
        free(place);
        if (!stored) {
            fprintf(stderr, "no Value property found\n");
            return 1;
        }

        int ok = write_file(path, stored, stored_len);
        free(stored);
        if (!ok) {
            fprintf(stderr, "could not write %s\n", path);
            return 1;
        }
        return 0;
    }

    fprintf(stderr, "unknown command: %s\n", command);
    return 1;
}
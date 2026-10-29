#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <fetch.h>
#include <curl/curl.h>

// libcurl bs
typedef struct {
    uint8_t *data;
    size_t size;
} Memory;

static size_t on_data(void *piece, size_t item_size, size_t item_count, void *user) {
    Memory *mem = user;
    size_t piece_size = item_size * item_count;

    uint8_t *bigger = realloc(mem->data, mem->size + piece_size + 1);
    if (!bigger) return 0;

    mem->data = bigger;
    memcpy(mem->data + mem->size, piece, piece_size);
    mem->size += piece_size;
    mem->data[mem->size] = 0;
    return piece_size;
}

int show_progress(void *user, curl_off_t download_total, curl_off_t download_now, curl_off_t upload_total, curl_off_t upload_now) {
    Progress *progress = user;
    curl_off_t total = progress->is_upload ? upload_total : download_total;
    curl_off_t now = progress->is_upload ? upload_now   : download_now;

    if (total <= 0) {
        fprintf(stderr, "\r%s %.2f MB", progress->label, now / 1048576.0);
        fflush(stderr);
        return 0;
    }

    const int width = 30;
    int filled = (int)(now * width / total);
    int percent = (int)(now * 100 / total);

    fprintf(stderr, "\r%s [", progress->label);
    for (int i = 0; i < width; i++) fputc(i < filled ? '#' : '-', stderr);
    fprintf(stderr, "] %3d%%  %.2f / %.2f MB", percent, now / 1048576.0, total / 1048576.0);
    fflush(stderr);
    return 0;                                
}

static int http_get(const char *url, const char *api_key, Memory *out, const char *label) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    struct curl_slist *headers = NULL;
    if (api_key) {
        char line[1024];
        snprintf(line, sizeof line, "x-api-key: %s", api_key);
        headers = curl_slist_append(headers, line);
    }

    Progress progress = { label, 0 };
    if (label) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, show_progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); 

    CURLcode result = curl_easy_perform(curl);
    if (label) {
        fputc('\n', stderr); 
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        fprintf(stderr, "curl failed: %s\n", curl_easy_strerror(result));
        return 0;
    }
    if (status != 200) {
        fprintf(stderr, "HTTP %ld: %s\n", status, out->data ? (char *)out->data : "");
        return 0;
    }
    return 1;
}

uint8_t *download_place(const char *api_key, uint64_t place_id, int version, size_t *out_size) {
    char url[256];
    if (version > 0) {
        snprintf(url, sizeof url, "https://apis.roblox.com/asset-delivery-api/v1/assetId/%" PRIu64 "/version/%d", place_id, version);
    } else {
        snprintf(url, sizeof url, "https://apis.roblox.com/asset-delivery-api/v1/assetId/%" PRIu64, place_id);
    }

    Memory reply = { 0 };
    if (!http_get(url, api_key, &reply, NULL)) {
        free(reply.data);
        return NULL;
    }

    if (reply.size >= 8 && memcmp(reply.data, "<roblox!", 8) == 0) {
        *out_size = reply.size;
        return reply.data;
    }

    char *key = strstr((char *)reply.data, "\"location\"");
    if (!key) {
        fprintf(stderr, "unexpected reply: %s\n", (char *)reply.data);
        free(reply.data);
        return NULL;
    }
    char *start = strchr(key + 10, '"');
    char *end = start ? strchr(start + 1, '"') : NULL;
    if (!start || !end) {
        free(reply.data);
        return NULL;
    }

    *end = 0;
    Memory file = {0};

    int ok = http_get(start + 1, NULL, &file, "downloading");
    free(reply.data);
    if (!ok) {
        free(file.data);
        return NULL;
    }

    *out_size = file.size;
    return file.data;
}

int upload_place(const char *api_key, uint64_t universe_id, uint64_t place_id, const uint8_t *file, size_t file_size, const char *label) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char url[256];
    snprintf(url, sizeof url, "https://apis.roblox.com/universes/v1/%" PRIu64 "/places/%" PRIu64 "/versions?versionType=Saved", universe_id, place_id);

    char key_header[1024];
    snprintf(key_header, sizeof key_header, "x-api-key: %s", api_key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, key_header);

    Memory reply = {0};
    Progress progress = { label, 1 };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)file_size);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, file);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, show_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);

    CURLcode result = curl_easy_perform(curl);
    fputc('\n', stderr);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    int version = -1;
    if (result != CURLE_OK) {
        fprintf(stderr, "curl failed: %s\n", curl_easy_strerror(result));
    } else if (status != 200) {
        fprintf(stderr, "upload failed, HTTP %ld: %s\n", status, reply.data ? (char *)reply.data : "");
    } else if (reply.data) {
        char *tag = strstr((char *)reply.data, "\"versionNumber\"");
        char *colon = tag ? strchr(tag, ':') : NULL;
        if (colon) {
            version = (int)strtol(colon + 1, NULL, 10);
        }
    }

    free(reply.data);
    return version;
}
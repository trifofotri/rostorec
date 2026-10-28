#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <fetch.h>
#include <curl/curl.h>

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

static int http_get(const char *url, const char *api_key, Memory *out) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    struct curl_slist *headers = NULL;
    if (api_key) {
        char line[1024];
        snprintf(line, sizeof line, "x-api-key: %s", api_key);
        headers = curl_slist_append(headers, line);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); 

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
    if (!http_get(url, api_key, &reply)) {
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
    
    int ok = http_get(start + 1, NULL, &file);
    free(reply.data);
    if (!ok) {
        free(file.data);
        return NULL;
    }

    *out_size = file.size;
    return file.data;
}
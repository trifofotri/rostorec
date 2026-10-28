#ifndef FETCH_H
#define FETCH_H

#include <curl/system.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *label;
    int is_upload;
} Progress;

uint8_t *download_place(const char *api_key, uint64_t place_id, int version, size_t *out_size);
uint8_t *extract_value(const uint8_t *file, size_t file_size, uint32_t *out_len);
int show_progress(void *user, curl_off_t download_total, curl_off_t download_now, curl_off_t upload_total, curl_off_t upload_now);

#endif
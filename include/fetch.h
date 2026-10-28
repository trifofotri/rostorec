#ifndef FETCH_H
#define FETCH_H

#include <stddef.h>
#include <stdint.h>

uint8_t *download_place(const char *api_key, uint64_t place_id, int version, size_t *out_size);
uint8_t *extract_value(const uint8_t *file, size_t file_size, uint32_t *out_len);

#endif
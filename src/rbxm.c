#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rbxm.h>
#include <lz4.h>

static void add_bytes(uint8_t **cursor, const void *src, size_t size) {
    memcpy(*cursor, src, size);
    *cursor += size;
}

static void add_chunk(uint8_t **cursor, const char *name, const void *payload, uint32_t size) {
    RBXMChunkHeader chunk =  { 0 };
    memcpy(chunk.name, name, 4);

    chunk.compressed_len = 0;
    chunk.uncompressed_len = size;

    add_bytes(cursor, &chunk, sizeof chunk);
    add_bytes(cursor, payload, size);
}

static void add_string_prop(uint8_t **cursor, const char *prop_name, const uint8_t *value, uint32_t value_len) {
    uint32_t class_id = 2;
    uint32_t name_len = (uint32_t) strlen(prop_name);
    uint8_t type_string = 0x01;
    uint32_t payload_size = 4 + 4 + name_len + 1 + 4 + value_len;

    RBXMChunkHeader chunk = {0};
    memcpy(chunk.name, "PROP", 4);
    chunk.uncompressed_len = payload_size;

    add_bytes(cursor, &chunk, sizeof chunk);
    add_bytes(cursor, &class_id, 4);
    add_bytes(cursor, &name_len, 4);
    add_bytes(cursor, prop_name, name_len);
    add_bytes(cursor, &type_string, 1);
    add_bytes(cursor, &value_len, 4);
    add_bytes(cursor, value, value_len);
}

uint8_t *build_place(const uint8_t *data, uint32_t data_len, size_t *out_size) {
    uint8_t *file = malloc(1024 + data_len);
    if (!file) {
        return NULL;
    }
    uint8_t *cursor = file;

    RBXMHeader header = { 0 };
    memcpy(header.roblox, "<roblox!", 8);
    memcpy(header.signature, "\x89\xff\x0d\x0a\x1a\x0a", 6);

    header.class_count = 3;
    header.instance_count = 3;
    add_bytes(&cursor, &header, sizeof(header));

    add_chunk(&cursor, "INST", inst_datamodel, sizeof(inst_datamodel));
    add_chunk(&cursor, "INST", inst_workspace, sizeof(inst_workspace));
    add_chunk(&cursor, "INST", inst_stringvalue, sizeof(inst_stringvalue));
    add_string_prop(&cursor, "Name", (const uint8_t *) "Data", 4);
    add_string_prop(&cursor, "Value", data, data_len);
    add_chunk(&cursor, "PRNT", prnt, sizeof(prnt));
    add_chunk(&cursor, "END\0", end, 9);

    *out_size = (size_t) ( cursor - file );
    return file;
}

static uint8_t *value_from_prop(const uint8_t *payload, uint32_t size, uint32_t *out_len) {
    if (size < 18) return NULL; // class id + name length + name + type + value

    uint32_t name_len;
    memcpy(&name_len, payload + 4, 4);
    if (name_len != 5 || memcmp(payload + 8, "Value", 5) != 0) {
        return NULL;
    }

    if (payload[13] != 0x01) return NULL;

    uint32_t value_len;
    memcpy(&value_len, payload + 14, 4);
    if (value_len > size - 18) return NULL;

    uint8_t *copy = malloc(value_len);
    if (!copy) return NULL;
    memcpy(copy, payload + 18, value_len);
    
    *out_len = value_len;
    return copy;
}

uint8_t *extract_value(const uint8_t *file, size_t file_size, uint32_t *out_len) {
    if (file_size < sizeof(RBXMHeader) || memcmp(file, "<roblox!", 8) != 0) {
        return NULL;
    }

    size_t pos = sizeof(RBXMHeader);
    while (pos + sizeof(RBXMChunkHeader) <= file_size) {
        RBXMChunkHeader chunk;
        memcpy(&chunk, file + pos, sizeof chunk);
        pos += sizeof chunk;

        uint32_t stored = chunk.compressed_len ? chunk.compressed_len : chunk.uncompressed_len;
        if (stored > file_size - pos) return NULL;

        if (memcmp(chunk.name, "PROP", 4) == 0) {
            const uint8_t *payload = file + pos;
            uint8_t *unpacked = NULL;

            if (chunk.compressed_len) {
                unpacked = malloc(chunk.uncompressed_len);
                if (!unpacked) return NULL;
                

                int got = LZ4_decompress_safe((const char *) payload, (char *) unpacked, (int) chunk.compressed_len, (int) chunk.uncompressed_len);
                if (got < 0 || (uint32_t)got != chunk.uncompressed_len) {
                    free(unpacked);
                    return NULL;
                }
                payload = unpacked;
            }

            uint8_t *value = value_from_prop(payload, chunk.uncompressed_len, out_len);
            free(unpacked);
            if (value) return value;
        }

        if (memcmp(chunk.name, "END", 3) == 0) break;
        pos += stored;
    }
    return NULL;
}
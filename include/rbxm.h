#ifndef RBXM_H
#define RBXM_H

#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 1)

typedef struct RBXMHeader {
    char roblox[8];
    char signature[6];
    uint16_t version;
    uint32_t class_count;
    uint32_t instance_count;
    char _reserved[8];
} RBXMHeader;

typedef struct RBXMChunkHeader {
    char name[4];
    uint32_t compressed_len;
    uint32_t uncompressed_len;
    char _reserved[4];
} RBXMChunkHeader;

#pragma pack(pop)

static const uint8_t inst_datamodel[] = {
    0,0,0,0,  9,0,0,0, 'D','a','t','a','M','o','d','e','l', 0, 1,0,0,0, 0,0,0,0
};

static const uint8_t inst_workspace[] = {
    1,0,0,0,  9,0,0,0, 'W','o','r','k','s','p','a','c','e', 1, 1,0,0,0, 0,0,0,2,  1
};

static const uint8_t inst_stringvalue[] = {
    2,0,0,0,  11,0,0,0, 'S','t','r','i','n','g','V','a','l','u','e',  0,  1,0,0,0,  0,0,0,4
};

static const uint8_t prnt[] = {
    0,
    3,0,0,0,
    0,0,0, 0,0,0, 0,0,0, 0,2,2,
    0,0,0, 0,0,0, 0,0,0, 1,2,2
};

static const uint8_t end[] = "</roblox>";

uint8_t *build_place(const uint8_t *data, uint32_t data_len, size_t *out_size);

#endif
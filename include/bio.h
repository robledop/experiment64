#pragma once

#include <stdint.h>
#include <stddef.h>

#define BIO_BLOCK_SIZE 512

typedef struct buffer_head
{
    uint8_t device;
    uint32_t block; // LBA
    uint8_t *data;
    uint8_t flags;
    uint32_t ref_count;
    struct buffer_head *next; // For LRU or Hash
    struct buffer_head *prev;
} buffer_head_t;

#define BIO_FLAG_VALID 0x01
#define BIO_FLAG_DIRTY 0x02

void bio_init(void);
buffer_head_t *bread(uint8_t device, uint32_t block);
void bwrite(buffer_head_t *bh);
void brelse(buffer_head_t *bh);

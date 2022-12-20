#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H
#include "devices/block.h"
#include <stdint.h>
#include "threads/synch.h"

struct cache
  {
    /* True if dirty */
    bool dirty;
    /* True if valid */
    bool valid;
    /* Corresponding sector index */
    block_sector_t sector;
    /* Last accessed time */
    uint32_t last_accessed_time;
    /* Data */
    uint8_t data[BLOCK_SECTOR_SIZE];
    /* Lock for cache line */
    struct lock lock;
  };

void cache_read(block_sector_t sector,void * buffer);
void cache_write(block_sector_t sector,void * buffer);
void cache_init();
void cache_done();
#endif
#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include <list.h>
#include "devices/block.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <stdbool.h>

struct buffer_cache_node
{
  block_sector_t sector;             /* Sector ID */
  bool dirty_bit;                    /* For eviction */
  bool accessed_bit;                 /* For eviction */
  struct list_elem elem;             /* To create eviction list */
  uint8_t data[BLOCK_SECTOR_SIZE];   /* Cached data from sector */
};

uint32_t buffer_cache_map[64];

void buffer_cache_init(void);

void buffer_cache_find(block_sector_t sector);
void buffer_cache_add(block_sector_t sector);
void buffer_cache_evict(void);

void buffer_cache_read(block_sector_t sector);
void buffer_cache_write(block_sector_t sector);

void buffer_cache_writeback(void);
void buffer_cache_readahead(void);

#endif /* filesys/buffer-cache.h */

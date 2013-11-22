#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include <stdbool.h>
#include "devices/block.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <list.h>
#include "threads/synch.h"

struct buffer_cache_node
{
  block_sector_t sector;             /* Sector ID */
  bool dirty_bit;                    /* For eviction */
  bool accessed_bit;                 /* For eviction */
  struct list_elem elem;             /* To create eviction list */
  uint8_t data[BLOCK_SECTOR_SIZE];   /* Cached data from sector */
};

struct list buffer_cache_list;
struct lock buffer_cache_lock;

void buffer_cache_init(void);

void buffer_cache_find(void);
void buffer_cache_add(void);
void buffer_cache_evict(void);

void buffer_cache_read(void);
void buffer_cache_write(void);

void buffer_cache_writeback(void);
void buffer_cache_readahead(void);

#endif /* filesys/buffer-cache.h */

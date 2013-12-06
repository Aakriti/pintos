#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include <list.h>
#include "hash.h"
#include "devices/block.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <stdbool.h>
#include "threads/synch.h"

struct buffer_cache_node
{
  block_sector_t sector;             /* Sector ID */

  bool dirty_bit;                    /* For eviction */
  bool accessed_bit;                 /* For eviction */

  //int readers_count;
  //int writers_count;

  struct lock buffer_lock;

  uint8_t data[BLOCK_SECTOR_SIZE];   /* Cached data from sector */

  struct list_elem elem;             /* To create eviction list */
  //struct hash_elem e;              /* To enable fast lookup; hash key is sector */
};

void buffer_cache_init(void);
void buffer_cache_flush(void);

struct buffer_cache_node * buffer_cache_find(block_sector_t sector);
struct buffer_cache_node * buffer_cache_add(block_sector_t sector);
struct buffer_cache_node * buffer_cache_evict(void);
struct buffer_cache_node * get_buffer_cache();

void buffer_cache_read(block_sector_t sector, uint8_t *ubuffer, int sector_ofs, int size);
void buffer_cache_write(block_sector_t sector, uint8_t *ubuffer, int sector_ofs, int size);

void buffer_cache_writeback(struct buffer_cache_node *node);
void buffer_cache_readahead(block_sector_t sector);

#endif /* filesys/buffer-cache.h */

#include "filesys/buffer-cache.h"
#include "threads/synch.h"

struct list buffer_cache_list;
struct lock buffer_cache_lock;

struct buffer_cache_node buffer_cache[64];


void buffer_cache_init(void)
{
  list_init(&buffer_cache_list);
  lock_init (&buffer_cache_lock);

  /* Mark all buffers as free in the map */
  int i;
  for(i=0; i<64; i++)
     buffer_cache_map[i] = 0; /* Is 0 a valid buffer sector? */
}

void buffer_cache_find(void)
{

}

void buffer_cache_add(void)
{

}

void buffer_cache_evict(void)
{

}

void buffer_cache_read(void)
{

}

void buffer_cache_write(void)
{

}

void buffer_cache_writeback(void)
{

}

void buffer_cache_readahead(void)
{

}

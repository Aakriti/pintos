#include "filesys/buffer-cache.h"

struct list buffer_cache_list;
struct lock cache_list_lock;  /* for atomic buffer_cache_list manipulation */
struct buffer_cache_node buffer_cache[64];

/* Initialize buffer cache */
void buffer_cache_init(void)
{
  list_init(&buffer_cache_list);
  lock_init(&cache_list_lock);

  /* Initialize all the free buffers */
  int i;
  for(i=0; i<64; i++)
  {
    buffer_cache[i].sector = block_size(fs_device) + 1;

    buffer_cache[i].dirty_bit = false;
    buffer_cache[i].accessed_bit = false;

    //buffer_cache[i].readers_count = 0;
    //buffer_cache[i].writers_count = 0;

    lock_init(&(buffer_cache[i].buffer_lock));

    list_push_back(&buffer_cache_list, &(buffer_cache[i].elem));
    /* TODO: can populate hash table here for fast lookup later */
  }
}

/* Function that writes back all the dirty sectors into fs_device
   To be called from filesys_done */
void buffer_cache_flush(void)
{
  int i;
  for(i=0; i<64; i++)
  {
    if(buffer_cache[i].dirty_bit)
      buffer_cache_writeback(&buffer_cache[i]);
  }
}


/* Find if a particular sector exists in the cache, if not return NULL */
struct buffer_cache_node * buffer_cache_find(block_sector_t sector)
{
  /* TODO: could use hash table here instead of for loop */

  int i;
  struct buffer_cache_node *node;
  for(i=0;i<64;i++)
  {
    node = &buffer_cache[i];

    /* sector value should be read within locks as it might get changed */
    lock_acquire(&node->buffer_lock);
    if(node->sector == sector)
    {
      lock_release(&node->buffer_lock);
      return node;
    }
    lock_release(&node->buffer_lock);
  }
  return NULL;
}

/* Add a new sector in the cache */
struct buffer_cache_node * buffer_cache_add(block_sector_t sector)
{
  struct buffer_cache_node *node;
  /* Check if same sector is already present */
  node = buffer_cache_find(sector);

  if(node != NULL)
  {
    /* push it to the back of the list if it already exists */
    lock_acquire(&cache_list_lock);

    /* Possibly, this node might not be in the list and could be going through eviction */
    /* In that case, it's too late to do anything about it and we go ahead with getting a new buffer */
    /* But... we should wait for the write back to be completed to get the latest changes */

    /* Remove node from current position and push it at the back */
    list_remove(&node->elem);
    list_push_back (&buffer_cache_list, &node->elem);

    lock_release(&cache_list_lock);
    return node;
  }

  /* If not already present, get a new buffer and load contents in it */
  node = get_buffer_cache();
  if(node == NULL)
  {
    printf("Can't allocate buffer cache\n");
    return NULL;
  }

  /* Now we bring the contents in the new buffer */
  lock_acquire(&node->buffer_lock);

  /* read sector from fs_device */
  block_read(fs_device, sector, node->data);

  /* mark buffer as accessed and update other buffer elements */
  node->accessed_bit = true;
  node->dirty_bit = false;
  node->sector = sector;

  /* TODO: coult do hash table updation here */

  lock_release(&node->buffer_lock);

  /* After block read completes, push node at the end of the list */
  lock_acquire(&cache_list_lock);
  list_push_back (&buffer_cache_list, &node->elem);
  lock_release(&cache_list_lock);
  return node;
}

/* Evict one frame from buffer cache and return pointer to it */
struct buffer_cache_node * buffer_cache_evict(void)
{
  int i;
  struct buffer_cache_node *node;
  for(i=0;i<64;i++)
  {
    node = &buffer_cache[i];

    lock_acquire(&node->buffer_lock);
    if(!node->accessed_bit)
    {
      lock_release(&node->buffer_lock);
      return node;
    }
    lock_release(&node->buffer_lock);
  }
  /* TODO: Eviction policy */

  node = &buffer_cache[0];
  lock_acquire(&node->buffer_lock);
  if(node->dirty_bit)
      buffer_cache_writeback(node);
  lock_release(&node->buffer_lock);

  return node;  
}

/* Get a free buffer from cache. If none is free, call eviction */
/* This function returns a free buffer after removing it from buffer_cache_list */
struct buffer_cache_node * get_buffer_cache()
{
  struct buffer_cache_node *node;
  node = buffer_cache_find(block_size(fs_device) + 1);

  if(node == NULL)
  {
    node = buffer_cache_evict();
  }


  lock_acquire(&node->buffer_lock);
  /* If dirty node is returned, write back its contents */
  if(node->dirty_bit)
    buffer_cache_writeback(node);
  lock_release(&node->buffer_lock);

  /* Remove node from list */
  lock_acquire(&cache_list_lock);
  list_remove(&node->elem);
  lock_release(&cache_list_lock);

  return node;
}

void buffer_cache_read(block_sector_t sector, uint8_t *ubuffer, int sector_ofs, int size)
{
  struct buffer_cache_node * node = buffer_cache_add(sector);

  /* Do away with locks here for concurrency */
  lock_acquire(&node->buffer_lock);
  /* Copy contents from cache buffer into user buffer */
  memcpy (ubuffer, &node->data[sector_ofs], size);
  node->accessed_bit = true;
  lock_release(&node->buffer_lock);
}

void buffer_cache_write(block_sector_t sector, uint8_t *ubuffer, int sector_ofs, int size)
{
  struct buffer_cache_node * node = buffer_cache_add(sector);

  /* Do away with locks here for concurrency */
  lock_acquire(&node->buffer_lock);
  /* Copy contents from user buffer into cache buffer*/
  memcpy (&node->data[sector_ofs], ubuffer, size);
  node->accessed_bit = true;
  node->dirty_bit = true;
  lock_release(&node->buffer_lock);
}

/* Write the contents of cache back to the device 
   Lock for the buffer must be held before calling write back for concurrency */
void buffer_cache_writeback(struct buffer_cache_node *node)
{
  if(node == NULL)
  {
    return;
  }
  /* write sector back to fs_device */
  block_write (fs_device, node->sector, node->data);
  node->accessed_bit = false;
  node->dirty_bit = false;
}


/* Prefetch the next sector from device into the cache */
void buffer_cache_readahead(block_sector_t sector)
{
  /* If this is the last sector, we can't prefetch! */
  if(sector >= block_size(fs_device))
  {
    return;
  }
  /* Else, we just call buffer_add here with next sector */
  struct buffer_cache_node * node = buffer_cache_add(sector+1);
}

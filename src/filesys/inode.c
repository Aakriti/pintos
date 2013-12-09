#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT 123
#define INDIRECT 1
#define DB_INDIRECT 1
#define SECTORS 125
#define BLOCK_PTRS (off_t)(BLOCK_SECTOR_SIZE / sizeof (block_sector_t))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t sector[SECTORS];     /* The sectors. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t inode_type;                /* 0=File_type and 1=Dir_type */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    bool is_dir;                        /* CADroid: True if inode represents a dir, false otherwise */
    block_sector_t parent;              /* CADroid: Parent pointer to implement .. in file paths */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock deny_write_lock;		/* For deny_write_cnt protection. */
    struct condition no_writers;		/* Writers wait on for condition. */
    int num_writers;					/* Number of writers. */
  };

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Lock for the shared accessing of open_inodes list. */
static struct lock open_inodes_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&open_inodes_lock);
}

/* Initializes an inode of given type, File or Dir and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  struct buffer_cache_node *cache_block;

  cache_block = buffer_cache_add (sector);
  if (cache_block == NULL)
    return false;
  
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  
  struct inode_disk ubuffer;
  memset (&ubuffer, 0, BLOCK_SECTOR_SIZE);
  disk_inode = &ubuffer;  
  disk_inode->length = 0;
  disk_inode->magic = INODE_MAGIC;
  disk_inode->inode_type = (is_dir == true)? 1 : 0;
  
  buffer_cache_write (sector, (uint8_t *)disk_inode, 0, BLOCK_SECTOR_SIZE);  
  buffer_cache_writeback (buffer_cache_add(sector)); 
  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire (&open_inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode->open_cnt++;
          lock_release (&open_inodes_lock);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
  {
	lock_release (&open_inodes_lock);
    return NULL;
  }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->deny_write_lock);
  cond_init (&inode->no_writers);
  
  // Setting is_dir flag
  struct inode_disk disk_inode;  
  buffer_cache_read(sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);
  inode->is_dir = (disk_inode.inode_type == 1)? 1:0;
  
  if(inode->is_dir)
  {
    struct inode* p_inode;
    struct dir *dir;
    
    dir = dir_open(inode);
    ASSERT (dir != NULL );
    if (dir_lookup (dir, "..", &p_inode))
      inode->parent = p_inode->sector;
  }
  else
    inode->parent = 1;
  
  lock_release (&open_inodes_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
  {
    lock_acquire (&open_inodes_lock);
    inode->open_cnt++;
    lock_release (&open_inodes_lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Deallocates the sectors for till the level specified,
   0 level => data, 1 level => Direct Sectors block and
   2 level => Indirect sectors block. */
static void
deallocate (block_sector_t sector, int level)
{
  if (level>0)
  {
    struct inode_disk block;
  	buffer_cache_read (sector, (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);
  	block_sector_t *ptr;
  	ptr = (block_sector_t *)&block;
  	int i;
  	for (i=0; i<BLOCK_PTRS; i++)
  	  if (ptr[i])
  	    deallocate (ptr[i], level-1);
  }
  buffer_cache_free_node (sector);
  free_map_release (sector, 1);
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  lock_acquire (&open_inodes_lock);
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      lock_release (&open_inodes_lock);
 
      /* Deallocate blocks if removed. */
      if (inode->removed)
      {
  		struct inode_disk disk_inode;
  		buffer_cache_read(inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);
  		int i;
  		for (i=0; i<SECTORS; i++)
  		{
  		  if (disk_inode.sector[i])
  		  {
  		    int level = 0;			//DIRECT
  		    if (i >= DIRECT) level++;
  		    if (i >= DIRECT+INDIRECT) level++;
  		    deallocate (disk_inode.sector[i], level); 
  		  }
  		}
  		deallocate (inode->sector, 0); 
      }
  	  else
  	  {  	  
  	    buffer_cache_writeback(buffer_cache_add(inode->sector));
  	  }
      free (inode);
    }
   else
     lock_release (&open_inodes_lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* To get the sector_idx for reading the inode with a
   given offset */
static block_sector_t*
get_sector_idx (struct inode *inode, off_t offset)
{
  block_sector_t *sector;
  int sector_off = offset / BLOCK_SECTOR_SIZE;
  
  /* Read the sector of inode */
  struct inode_disk disk_inode;
  buffer_cache_read (inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);

  /* DIRECT if sector_off < DIRECT, INDIRECT if sector_off < (DIRECT+
     BLOCK_PTRS) else DB_INDIRECT */
  if (sector_off < DIRECT)
  {
    buffer_cache_readahead(disk_inode.sector[sector_off+1]);
    sector = (block_sector_t *)&(disk_inode.sector[sector_off]);          
  }
  else if (sector_off < DIRECT+BLOCK_PTRS)
  {
    struct inode_disk block;
    buffer_cache_read (disk_inode.sector[DIRECT], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);
    block_sector_t *ptr;
    ptr = (block_sector_t *)&block;
    if ((sector_off - DIRECT+1) != BLOCK_PTRS)
      buffer_cache_readahead(ptr[sector_off - DIRECT+1]);
    else
      buffer_cache_readahead(disk_inode.sector[DIRECT+INDIRECT]);
    sector = (block_sector_t *)&(ptr[sector_off - DIRECT]);
  }
  else
  {
    struct inode_disk db_block;
    buffer_cache_read (disk_inode.sector[DIRECT+INDIRECT], (uint8_t *)&db_block, 0, BLOCK_SECTOR_SIZE);
    block_sector_t *db_ptr;
    db_ptr = (block_sector_t *)&db_block;
    int d_off = (sector_off - DIRECT+BLOCK_PTRS) / BLOCK_PTRS;
    struct inode_disk block;
    buffer_cache_read (db_ptr[d_off], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);
    block_sector_t *ptr;
    ptr = (block_sector_t *)&block;
    if ((d_off / BLOCK_PTRS) +1 != BLOCK_PTRS)
      buffer_cache_readahead(ptr[(d_off / BLOCK_PTRS) +1]);
    else
      buffer_cache_readahead(db_ptr[d_off+1]);
    sector = (block_sector_t *)&(ptr[d_off / BLOCK_PTRS]);
  }
  return sector;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
       //= byte_to_sector (inode, offset);
      block_sector_t *read_sector;
      read_sector = get_sector_idx (inode, offset);      
      block_sector_t sector_idx = *read_sector;
      
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read from buffer cache */
      buffer_cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  return bytes_read;
}

/* Fill up with size zeros from offset for the inode*/
static off_t
inode_write_zero (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  while (size > 0) 
    {
      /* Starting byte offset within sector. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Max Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = ((DIRECT+(INDIRECT*BLOCK_PTRS)+
      			(DB_INDIRECT*BLOCK_PTRS*BLOCK_PTRS))*BLOCK_SECTOR_SIZE) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

	  block_sector_t sector_idx = (uint8_t)-1;

      /*  Allocation if needed.*/
	  block_sector_t *write_sector;
      int sector_off = offset / BLOCK_SECTOR_SIZE;

      /* Read the sector of inode */
      struct inode_disk disk_inode;
      buffer_cache_read (inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);

      /* DIRECT if sector_off < DIRECT, INDIRECT if sector_off < (DIRECT+
         BLOCK_PTRS) else DB_INDIRECT */
	  if (sector_off < DIRECT)
	  {
		write_sector = (block_sector_t *)&(disk_inode.sector[sector_off]);
		if (!(*write_sector) && !free_map_allocate (1, write_sector))
		  break;
		sector_idx = *write_sector;
		buffer_cache_write (inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);		  
      }
	  else if (sector_off < DIRECT+BLOCK_PTRS)
	  {
		struct inode_disk block;
		buffer_cache_read (disk_inode.sector[DIRECT], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);
		block_sector_t *ptr;
		ptr = (block_sector_t *)&block;
		write_sector = (block_sector_t *)&(ptr[sector_off - DIRECT]);
		if (!(*write_sector) && !free_map_allocate (1, write_sector))
		  break;
		sector_idx = *write_sector;
		buffer_cache_write (disk_inode.sector[DIRECT], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);
	  }
	  else
	  {
		struct inode_disk db_block;
		buffer_cache_read (disk_inode.sector[DIRECT+INDIRECT], (uint8_t *)&db_block, 0, BLOCK_SECTOR_SIZE);
		block_sector_t *db_ptr;
		db_ptr = (block_sector_t *)&db_block;
		int d_off = (sector_off - DIRECT+BLOCK_PTRS) / BLOCK_PTRS;
		struct inode_disk block;
		buffer_cache_read (db_ptr[d_off], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);
		block_sector_t *ptr;
		ptr = (block_sector_t *)&block;
		write_sector = (block_sector_t *)&(ptr[d_off / BLOCK_PTRS]);
		if (!(*write_sector) && !free_map_allocate (1, write_sector))
		  break;
		sector_idx = *write_sector;
		buffer_cache_write (db_ptr[d_off], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);  
	  }

      buffer_cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;      
    }
  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  /* Returning back if writes are denyed */
  lock_acquire (&inode->deny_write_lock);
  if (inode->deny_write_cnt)
  {
    lock_release (&inode->deny_write_lock);
    return 0;
  }
  inode->num_writers++;
  lock_release (&inode->deny_write_lock);

  /* Extending file with zeros if user tries to write 
     beyond the EOF. */
  off_t file_length = inode_length(inode);
  if (offset>file_length && size>0)
  {
	void *zero_buffer;
	off_t zero_size = offset - file_length;
	zero_buffer = malloc(zero_size);
	ASSERT (zero_buffer != NULL);
	lock_acquire (&inode->deny_write_lock);
	if (--inode->num_writers == 0)
      cond_signal (&inode->no_writers, &inode->deny_write_lock);
	lock_release (&inode->deny_write_lock);
	off_t written;
	written = inode_write_zero (inode, zero_buffer, zero_size, file_length);

	if (written != zero_size)
	{
	  struct inode_disk disk_inode;
      buffer_cache_read (inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);
      disk_inode.length = written;
      buffer_cache_write (inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);
      return bytes_written;
	}
  }

  while (size > 0) 
    {
      /* Starting byte offset within sector. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Max Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = ((DIRECT+(INDIRECT*BLOCK_PTRS)+
      			(DB_INDIRECT*BLOCK_PTRS*BLOCK_PTRS))*BLOCK_SECTOR_SIZE) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      block_sector_t sector_idx = (uint8_t)-1;

      /* Allocation if needed.*/
      block_sector_t *write_sector;
      int sector_off = offset / BLOCK_SECTOR_SIZE;

      /* Read the sector of inode */
      struct inode_disk disk_inode;
      buffer_cache_read (inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);

      /* DIRECT if sector_off < DIRECT, INDIRECT if sector_off < (DIRECT+
         BLOCK_PTRS) else DB_INDIRECT */
      if (sector_off < DIRECT)
      {
		write_sector = (block_sector_t *)&(disk_inode.sector[sector_off]);
		if (!(*write_sector) && !free_map_allocate (1, write_sector))
		  break;
		sector_idx = *write_sector;
		buffer_cache_write (inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);		  
      }
	  else if (sector_off < DIRECT+BLOCK_PTRS)
	  {
		struct inode_disk block;
		buffer_cache_read (disk_inode.sector[DIRECT], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);
		block_sector_t *ptr;
		ptr = (block_sector_t *)&block;
		write_sector = (block_sector_t *)&(ptr[sector_off - DIRECT]);
		if (!(*write_sector) && !free_map_allocate (1, write_sector))
		  break;
		sector_idx = *write_sector;
		buffer_cache_write (disk_inode.sector[DIRECT], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);
	  }
	  else
	  {
		struct inode_disk db_block;
		buffer_cache_read (disk_inode.sector[DIRECT+INDIRECT], (uint8_t *)&db_block, 0, BLOCK_SECTOR_SIZE);
		block_sector_t *db_ptr;
		db_ptr = (block_sector_t *)&db_block;
		int d_off = (sector_off - DIRECT+BLOCK_PTRS) / BLOCK_PTRS;
		struct inode_disk block;
		buffer_cache_read (db_ptr[d_off], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);
		block_sector_t *ptr;
		ptr = (block_sector_t *)&block;
		write_sector = (block_sector_t *)&(ptr[d_off / BLOCK_PTRS]);
		if (!(*write_sector) && !free_map_allocate (1, write_sector))
		  break;
		sector_idx = *write_sector;
		buffer_cache_write (db_ptr[d_off], (uint8_t *)&block, 0, BLOCK_SECTOR_SIZE);  
	  }

      buffer_cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;      
    }

  /* Extend if necessary. */
  if (offset > file_length)
  {
    struct inode_disk disk_inode;
    buffer_cache_read (inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);
    disk_inode.length = offset;
    buffer_cache_write (inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);
  }

  lock_acquire (&inode->deny_write_lock);
  if (--inode->num_writers == 0)
    cond_signal (&inode->no_writers, &inode->deny_write_lock);
  lock_release (&inode->deny_write_lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire (&inode->deny_write_lock);
  while (inode->num_writers >0)
    cond_wait (&inode->no_writers, &inode->deny_write_lock);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release (&inode->deny_write_lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  lock_acquire (&inode->deny_write_lock);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release (&inode->deny_write_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
//printf("+++++++++Inside inode_length finding for inode %x+++++++\n",inode);
  struct inode_disk disk_inode;
  buffer_cache_read(inode->sector, (uint8_t *)&disk_inode, 0, BLOCK_SECTOR_SIZE);
//printf("+++++++++Returning inode_length %d+++++++\n",disk_inode.length);  
  return disk_inode.length;
}

/* Returns true if inode represents a directory */
bool 
inode_is_dir(struct inode *inode)
{
  return inode->is_dir;
}

/* Returns parent sector of inode */
block_sector_t 
inode_get_parent(struct inode *inode)
{
  ASSERT (inode->is_dir == true);
  return inode->parent;
}

/* Returns open count of inode */
int
inode_get_count(struct inode *inode)
{
  return inode->open_cnt;
}

/* Returns parent sector of inode */
block_sector_t 
inode_get_sector(struct inode *inode)
{
  return inode->sector;
}


#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "filesys/buffer-cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  buffer_cache_init();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  buffer_cache_flush();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  if (strlen(name) > NAME_MAX)
    return false;

  block_sector_t inode_sector = 0;
  struct dir *dir = NULL;
  char token[strlen(name) + 1];

  if(!get_containing_folder(name, &dir, token))
    return false;

  ASSERT(dir != NULL);

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, false)
                  && dir_add (dir, token, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  dir_close (dir);
  struct inode *inode = inode_open (inode_sector);
  if (initial_size > 0 && inode_write_at (inode, "", 1, initial_size - 1) != 1)
  {
    inode_remove (inode);
    inode_close (inode);
    success = false;
  }
  else
    inode_close (inode);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct inode *inode = NULL;

  if(!resolve_path(name, &inode))
    return NULL;

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct thread *t = thread_current();
  struct dir *dir = NULL;
  char token[strlen(name) + 1];

  if(!get_dir(name, &dir, token))
    return false;

  bool success = dir != NULL && dir_remove (dir, token);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

bool
resolve_path(const char *path, struct inode **inode)
{
  if(*path == '\0' || strlen (path) < 1)
    return false;

  struct dir *dir;
  struct thread *t = thread_current();

  if(strlen (path) == 1 && *path == '.')
  {
    dir = dir_reopen(t->cur_dir);
    *inode = dir_get_inode(dir);
    dir_close(dir);
    return true;
  }
  
  if(strlen (path) == 1 &&  path[0] == '/')
  {
    *inode = inode_open (ROOT_DIR_SECTOR);
    return true;
  }

  if(path[0] == '/' || t->cur_dir == NULL)
    dir = dir_open_root();
  else
    dir = dir_reopen(t->cur_dir);

  char path_temp[strlen(path) + 1];
  memcpy(path_temp, path, strlen(path) + 1);

  char *token, *prev_token = NULL, *save_ptr;
  for (token = strtok_r(path_temp, "/", &save_ptr); ; token = strtok_r (NULL, "/", &save_ptr))
  {
    if(prev_token == NULL)
    {
      prev_token = token;
      goto next;
    }

    /* check for . and .. */
    if(strcmp(prev_token,".") == 0)
      goto next;

    if(strcmp(prev_token,"..") == 0)
      goto next;

    if(dir_lookup (dir, prev_token, inode))
    {
      if(token != NULL)
      {
        /* this isn't last token in the pathname */
        if(!inode_is_dir(*inode))
        {
          /* only the last token is allowed to be not a dir */
          inode_close(*inode);
          *inode = NULL;
          dir_close(dir);
          return false;
        }
        else
        {
          /* jump into next directory */
          dir_close(dir);
          dir = dir_open(*inode);
        }
      }
      else
      {
        /* last token in the pathname: maybe be file or dir */
        dir_close(dir);
        return true;
      }
   }
   else
   {
     inode_close(*inode);
     *inode = NULL;
     dir_close(dir);
     return false;
   }

   next:
     prev_token = token;
  }

  return false;
}

bool
get_containing_folder(const char *dirname, struct dir **dir, char *tok)
{
  struct thread *t = thread_current();
  struct inode *inode = NULL;
  char *token, *prev_token = NULL, *save_ptr;
  char path_temp[strlen(dirname) + 1];

  if(*dirname == '\0' || strlen (dirname) < 1)
    return false;

  if(strlen (dirname) == 1 && (*dirname == '.' || *dirname == '/'))
    return false;

  if(t->cur_dir == NULL || dirname[0] == '/')
    *dir = dir_open_root();
  else
    *dir = dir_reopen(t->cur_dir);

  if (*dir == NULL)
    return false;

  memcpy(path_temp, dirname, strlen(dirname) + 1);

  for (token = strtok_r(path_temp, "/", &save_ptr); ; token = strtok_r (NULL, "/", &save_ptr))
  {
    if(prev_token == NULL) {
      prev_token = token;
      goto next;
    }

    /* check for . and .. */
    if(strcmp(prev_token,".") == 0)
      goto next;

    if(strcmp(prev_token,"..") == 0)
      goto next;

    if(dir_lookup (*dir, prev_token, &inode))
    {
      if(token != NULL)
      {
        /* this isn't last token in the pathname */
        if(!inode_is_dir(inode))
        {
          /* not a dir */
          inode_close(inode);
          inode = NULL;
          dir_close(*dir);
          return false;
        }
        else
        {
          /* jump into next directory */
          dir_close(*dir);
          *dir = dir_open(inode);
        }
      }
      else
      {
        /* last token in the pathname => already exists */
        inode_close(inode);
        inode = NULL;
        dir_close(*dir);
        return false;
      }
    }
    else
    {
      if(token != NULL)
      {
        /* some intermediate directory doesn't exist */
        inode_close(inode);
        inode = NULL;
        dir_close(*dir);
        return false;
      }
      else
      {
        /* found the directory */
        memcpy(tok, prev_token, strlen(prev_token) + 1);
        return true;
      }
    }
    next:
    prev_token = token;
  }
  return false;
}


bool
get_dir(const char *dirname, struct dir **dir, char *tok)
{
  if(*dirname == '\0' || strlen (dirname) < 1)
    return false;

  if(strlen (dirname) == 1 && (*dirname == '.' || *dirname == '/'))
    return false;

  struct thread *t = thread_current();
  struct inode *inode = NULL;

  char *token, *prev_token = NULL, *save_ptr;
  char path_temp[strlen(dirname) + 1];
  memcpy(path_temp, dirname, strlen(dirname) + 1);

  if(t->cur_dir == NULL || dirname[0] == '/')
    *dir = dir_open_root();
  else
    *dir = dir_reopen(t->cur_dir);

  for (token = strtok_r(path_temp, "/", &save_ptr); ; token = strtok_r (NULL, "/", &save_ptr))
  {
    if(prev_token == NULL)
    {
      prev_token = token;
      goto next;
    }

    /* check for . and .. */
    if(strcmp(prev_token,".") == 0)
      goto next;
      
    if(strcmp(prev_token,"..") == 0)
      goto next;

    if(dir_lookup (*dir, prev_token, &inode))
    {
      if(token != NULL)
      {
        /* this isn't last token in the pathname */
        if(!inode_is_dir(inode))
        {
          /* not a dir */
          inode_close(inode);
          inode = NULL;
          dir_close(*dir);
          return false;
        }
        else
        {
          /* jump into next directory */
          dir_close(*dir);
          *dir = dir_open(inode);
        }
      }
      else
      {
        memcpy(tok, prev_token, strlen(prev_token) + 1);
        return true;
      }
    }
    else
    {
      inode_close(inode);
      inode = NULL;
      dir_close(*dir);
      return false;
    }
    next:
    prev_token = token;
  }
  return false;
}

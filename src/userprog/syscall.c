#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/vaddr.h"
#include "pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "devices/shutdown.h"



/* Aakriti: syscall.c */
static uint32_t arg0,arg1,arg2,call_no;
static int count = 2;

struct file_map
{
  struct file *f_l;
  int f_d;
  int p_id;
  struct file_map *next;
};
static struct file_map *map = NULL;


static void syscall_handler (struct intr_frame *);
static void halt (void);
static void exit (int status);
static int exec (const char *cmd_line);
static int wait (int pid);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);
static void * access_user_page (void *upage);
static int get_arg0(void *ptr);
static int get_arg1(void *ptr);
static int get_arg2(void *ptr);
static int f_map(struct file *f);
static struct file *f_unmap(int fd);
static int valid_fd(int fd);
static void f_remove(int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  if(access_user_page (f->esp) != NULL)
    call_no = *((uint32_t *)(f->esp));
  else
    exit (-1);

  switch(call_no)
  {
    case 0: halt(); break;

    case 1: if(!get_arg0(f->esp))
              exit (-1);
            exit((int)arg0); break;

    case 2: if(!get_arg0(f->esp))
              exit (-1);
            f->eax = exec((const char *)arg0); break;

    case 3: if(!get_arg0(f->esp))
              exit (-1);
            f->eax = wait((int)arg0); break;

    case 4: if(!get_arg1(f->esp))
              exit (-1);
            f->eax = create((const char *)arg0,(unsigned int)arg1); break;

    case 5: if(!get_arg0(f->esp))
              exit (-1);
            f->eax = remove((const char *)arg0); break;

    case 6: if(!get_arg0(f->esp))
              exit (-1);
            f->eax = open((const char *)arg0); break;

    case 7: if(!get_arg0(f->esp))
              exit (-1);
            f->eax = filesize((int)arg0); break;

    case 8: if(!get_arg2(f->esp))
              exit (-1);
            f->eax = read((int)arg0,(void *)arg1,(unsigned int)arg2); break;

    case 9: if(!get_arg2(f->esp))
              exit (-1);
            f->eax = write((int)arg0,(const void*)arg1,(unsigned int)arg2); break;

    case 10: if(!get_arg1(f->esp))
              exit (-1);
             seek((int)arg0,(unsigned)arg1); break;

    case 11: if(!get_arg0(f->esp))
              exit (-1);
             f->eax = tell((int)arg0); break;

    case 12: if(!get_arg0(f->esp))
              exit (-1);
             close((int)arg0); break;

    default: printf("Invalid Syscall Number %d\n",call_no); exit (-1);
  }
}

/* Function that checks if a user page is valid or not
   If not, it'll return NULL */
static void *access_user_page (void *upage)
{
  if(upage!=NULL && is_user_vaddr(upage))
    return pagedir_get_page(thread_current()->pagedir,upage);
  return NULL;
}

/* Function that checks if user provided pointer for 1st argument is valid or not */
static int get_arg0(void *ptr)
{
   if(access_user_page ((uint32_t *)ptr + 1) == NULL)
     return 0;

   arg0 = *((uint32_t *)ptr + 1);
   return 1;
}

/* Function that checks if user provided pointer for 1st & 2nd arguments is valid or not */
static int get_arg1(void *ptr)
{
   /*if(access_user_page ((uint32_t *)ptr + 1) == NULL)
     return 0;*/
   if(access_user_page ((uint32_t *)ptr + 2) == NULL)
     return 0;

   arg0 = *((uint32_t *)ptr + 1);
   arg1 = *((uint32_t *)ptr + 2);
   return 1;
}

/* Function that checks if user provided pointer for 1st,2nd & 3rd arguments is valid or not */
static int get_arg2(void *ptr)
{
   /*if(access_user_page ((uint32_t *)ptr + 1) == NULL)
     return 0;
   if(access_user_page ((uint32_t *)ptr + 2) == NULL)
     return 0;*/
   if(access_user_page ((uint32_t *)ptr + 3) == NULL)
     return 0;

   arg0 = *((uint32_t *)ptr + 1);
   arg1 = *((uint32_t *)ptr + 2);
   arg2 = *((uint32_t *)ptr + 3);
   return 1;
}

static int f_map(struct file *f)
{
  struct file_map *f_m;
  f_m = malloc(sizeof(struct file_map));
  if(f_m == NULL)
    return -1;
  f_m->f_l = f;
  f_m->f_d = count++;
  f_m->p_id = thread_current()->tid;
  f_m->next = map;
  map = f_m;

  return f_m->f_d;
}
static struct file *f_unmap(int fd)
{
  struct file_map *f_m = map;
  while(f_m->f_d != fd)
    f_m = f_m->next;
  return f_m->f_l;
}
static void f_remove(int fd)
{
  struct file_map *f1 = map,*f2 = NULL;
  while(f1 != NULL && f1->f_d != fd)
  {
    f2 = f1;
    f1 = f2->next;
  }
  if(f1 != NULL)
  {
    if(f2 != NULL)
      f2->next = f1->next;
    else
      map = map->next;
    free(f1);
  }
}
static int valid_fd(int fd)
{
  struct file_map *f_m = map;
  while(f_m != NULL && f_m->f_d != fd)
     f_m = f_m->next;
  if(f_m == NULL || f_m->p_id != thread_current()->tid)
    return 0;
  
  return f_m->f_d;
}
static void halt (void)
{
  shutdown_power_off();
}
static void exit (int status)
{
  int tid = thread_current()->tid;

  if(filesys_lock_holder() == thread_current())
     filesys_lock_release();

  file_cleanup(tid);
  update_pid_status(tid, status);
  thread_exit ();
}
static int exec (const char *cmd_line)
{
  if(access_user_page((void *)cmd_line) == NULL)
    exit (-1);

  int p = process_execute((const char *)pagedir_get_page (thread_current()->pagedir, cmd_line));

  if(p == TID_ERROR)
    return -1;

  while(get_pid_load(p) == 0)
  {
    thread_yield();
  }

  if(get_pid_load(p) == 1)
    return p;
  return -1;
}
static int wait (int pid)
{
  return process_wait(pid);
}
static bool create (const char *file, unsigned initial_size)
{
  if(access_user_page((void *)file) == NULL)
    exit (-1);

  filesys_lock_acquire();
  bool b = filesys_create (file, initial_size);
  filesys_lock_release();

  return b;
}
static bool remove (const char *file) 
{
  if(access_user_page((void *)file) == NULL)
    exit (-1);
  filesys_lock_acquire();
  bool b = filesys_remove (file);
  filesys_lock_release();

  return b;
}
static int open (const char *file)
{
  if(access_user_page((void *)file) == NULL)
    exit (-1);

  filesys_lock_acquire();
  struct file *f = filesys_open (file);
  filesys_lock_release();

  if(f==NULL)
    return -1;
  return f_map(f);
}
static int filesize (int fd)
{
  int sz;
  if(valid_fd(fd))
  {
    filesys_lock_acquire();
    sz = file_length (f_unmap(fd));
    filesys_lock_release();
  }
  else
    exit (-1);
  
  return sz;
}
static int read (int fd, void *buffer, unsigned size)
{
  if(access_user_page((void *)buffer) == NULL)
    exit (-1);

  int bytes_read;

  if(fd == STDIN_FILENO)
  {
    //input_getc();
    return size;
  }

  if(fd == STDOUT_FILENO)
  {
    exit(-1);
  }

  if(valid_fd(fd))
  {
    filesys_lock_acquire();
    bytes_read =  file_read (f_unmap(fd), buffer, size);
    filesys_lock_release();
  }
  else
    exit (-1);

  return bytes_read;
}
static int write (int fd, const void *buffer, unsigned size)
{
  if(access_user_page((void *)buffer) == NULL)
    exit (-1);

  int bytes_written;
  if(fd == STDOUT_FILENO)
  {
    putbuf (buffer, size);
    return size;
  }
  else if(fd == STDIN_FILENO)
  {
    exit(-1);
  }
  else
  {
    if(valid_fd(fd))
    {
      filesys_lock_acquire();
      bytes_written = file_write (f_unmap(fd), buffer, size);
      filesys_lock_release();
    }
    else
      exit (-1);
  } 
  return bytes_written;
}
static void seek (int fd, unsigned position)
{
  if(valid_fd(fd))
  {
    filesys_lock_acquire();
    file_seek (f_unmap(fd), position);
    filesys_lock_release();
  }
  else
    exit (-1);
}
static unsigned tell (int fd)
{
  unsigned pos;
  if(valid_fd(fd))
  {
    filesys_lock_acquire();
    pos = file_tell(f_unmap(fd));
    filesys_lock_release();
  }
  else
    exit (-1);

  return pos;
}
static void close (int fd)
{
  if(valid_fd(fd))
  {
    filesys_lock_acquire();
    file_close(f_unmap(fd));
    f_remove(fd);
    filesys_lock_release();
  }
  else
    exit (-1);
}

void file_cleanup(int tid)
{
  struct file_map *f2,*f1 = map;
  while(f1 != NULL)
  {
    if(f1->p_id == tid)
    {
      f2 = f1->next;
      f_remove(f1->f_d);
      f1 = f2;
    }
    else
      f1 = f1->next;
  }
}

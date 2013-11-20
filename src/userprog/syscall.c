#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "threads/synch.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  lock_init (&file_lock);		//CADroid: Initalize the global lock
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* CADroid: Check is the memory reference from the pointer of given size is vaild */ 
static void
check_memory (void *ptr, uint32_t size)
{
  bool flag = true;
  uint32_t *pagedir = thread_current ()->pagedir;
  
  /* check for NULL pointer */
  if (ptr == NULL)
    flag = false;
  else
    /* checks for start and end address belongs to user space */  
    flag = is_user_vaddr (ptr);
  
  if (flag && size>0) {
    flag = is_user_vaddr (ptr+size-1);
    
    /* checks for the page having start byte address is page mapped */ 
    if (flag && pagedir_get_page (pagedir, ptr) == NULL) {
      flag = false;
   
      /* checks for the page having end byte address is page mapped */ 
      if (flag && pagedir_get_page (pagedir, ptr) == NULL)
        flag = false;
     }
  }
  
  if (flag == false)
  {
    thread_current ()->exit_code = -1;
    thread_exit ();
  }
}

/* CADroid: Halt the operating system. */
static void
syscall_halt (void)
{
  shutdown_power_off ();
}

/* CADroid: Terminate this process. */
static void
syscall_exit (int status)
{
  thread_current ()->exit_code = status;
  thread_exit ();
}

/* CADroid: File name pointer check function */
static void
check_file (const char *file)
{
  if (file == NULL)
  {
    thread_current ()->exit_code = -1;
    thread_exit ();
  }  
  else
    check_memory ((void*)file, 1);
}

/* CADroid: Start another process. */
static tid_t
syscall_exec (const char *cmd_line)
{
  check_file (cmd_line);
  check_memory ((void*)cmd_line, strlen (cmd_line));
  
  tid_t tid = process_execute (cmd_line);
  if (tid == TID_ERROR)
    return -1;
  else
    return tid;
}

/* CADroid: Wait for a child process to die. */
static int
syscall_wait (tid_t pid)
{
  return process_wait (pid);
}

/* CADroid: Create a file. */
static bool
syscall_create (const char *file, unsigned initial_size)
{
  bool result;
  check_file (file);  
  check_memory ((void*)file, strlen (file));
  
  lock_acquire (&file_lock);
  result = filesys_create (file, initial_size);
  lock_release (&file_lock);
  return result;
}

/* CADroid: Delete a file. */
static bool
syscall_remove (const char *file)
{
  bool result;
  check_file (file);
  check_memory ((void*)file, strlen (file));
  
  lock_acquire (&file_lock);
  result = filesys_remove (file);
  lock_release (&file_lock);
  return result;
}

/* CADroid: Open a file. */
static int
syscall_open (const char *filename)
{
  check_file (filename);
  int len =  strlen (filename);
  check_memory ((void*)filename, len);
  
  struct file *file;
  lock_acquire (&file_lock);
  file =  filesys_open (filename);
  lock_release (&file_lock);
  if (file == NULL) return -1;

  /* file_dscptr is initalized and is pushed into
    fd_list of the current thread */
  struct file_dscptr *fd_ptr;
  struct thread *t = thread_current ();
  
  fd_ptr = malloc (sizeof(struct file_dscptr));
  if (fd_ptr == NULL) return -1;
  
  fd_ptr->fd_id = t->fd_id++;
  fd_ptr->file = file;

  /* push into the fd_list the created file descriptor */
  list_push_back (&t->fd_list, &fd_ptr->elem);
  
  return fd_ptr->fd_id;
}

/* CADroid: given fd_id for a thread get the file
   decriptor structure pointer */
static void*
get_file_dscptr (int fd)
{
  /* Don't have fd value less than 2 in fd_list*/
  if (fd<2) return NULL;
  
  struct file_dscptr *fd_ptr = NULL;
  struct thread *t = thread_current ();
  struct list_elem *e;
  for(e = list_begin (&t->fd_list); e != list_end (&t->fd_list); 
  					e = list_next (e))
  {
    fd_ptr = list_entry (e, struct file_dscptr, elem);
    if (fd_ptr->fd_id == fd) break;
  }
  
  return (void*)fd_ptr;
}

/* CADroid: Obtain a file's size. */
static int
syscall_filesize (int fd)
{
  int length;
  struct file_dscptr *fd_ptr;
  fd_ptr = (struct file_dscptr*)get_file_dscptr (fd); 
  
  if(fd_ptr == NULL) 
    return -1;
  else
  {
    lock_acquire (&file_lock);
    length = file_length (fd_ptr->file);
    lock_release (&file_lock);    
    return length;
  }
}

/* CADroid: Read from a file. */
static int
syscall_read (int fd, void *buffer_, unsigned size)
{ 
  check_memory ((void*)buffer_, size);
  int count = -1;

  char *buffer = (char*)buffer_;
  /* Reading from the keyboard */
  if (fd == 0)
  {
    count = 0;
    while (count < (int)size) {
      buffer[count] = input_getc ();
      count++;
    }
  }
  else
  {
    struct file_dscptr *fd_ptr;
    fd_ptr = (struct file_dscptr*)get_file_dscptr (fd); 

    if(fd_ptr == NULL) 
      return count;
    else 
    {
      lock_acquire (&file_lock);       
      count = file_read (fd_ptr->file, buffer, size);
      lock_release (&file_lock);
      return count;
    }
  }
  return count;
}

/* CADroid: Write to a file. */
static int
syscall_write (int fd, const void *buffer, unsigned size)
{
  int count;
  check_memory ((void*)buffer, size);

  /* Write to console if fd == 1 */
  if (fd == 1)
  {
    putbuf (buffer, size);
    return size;
  }
  else
  {
    struct file_dscptr *fd_ptr;
    fd_ptr = (struct file_dscptr*)get_file_dscptr (fd); 

    if(fd_ptr == NULL) 
      return 0;
    else
    {
      lock_acquire (&file_lock); 
      count = file_write (fd_ptr->file, buffer, size);
      lock_release (&file_lock);    
      return count;
    }
  }
}

/* CADroid: Change position in a file. */
static void 
syscall_seek (int fd, unsigned position) 
{
  struct file_dscptr *fd_ptr;
  fd_ptr = (struct file_dscptr*)get_file_dscptr (fd); 
  
  if(fd_ptr == NULL)
      return;
  else
  {
    lock_acquire (&file_lock);
    file_seek (fd_ptr->file, position);
    lock_release (&file_lock);
  }
}

/* CADroid: Report current position in a file. */
static unsigned 
syscall_tell (int fd)
{
  int pos;
  struct file_dscptr *fd_ptr;
  fd_ptr = (struct file_dscptr*)get_file_dscptr (fd); 
  
  if(fd_ptr == NULL)
      return -1;
  else
  {
    lock_acquire (&file_lock);
    pos = file_tell (fd_ptr->file);
    lock_release (&file_lock);
    return pos;
  }
}

/* CADroid: Close a file. */
void 
syscall_close (int fd)
{  
  struct file_dscptr *fd_ptr;
  fd_ptr = (struct file_dscptr*)get_file_dscptr (fd);
   
  if(fd_ptr == NULL) return;
  
  /* Clean up the memory */
  lock_acquire (&file_lock);
  file_close (fd_ptr->file);
  lock_release (&file_lock);
  list_remove (&fd_ptr->elem);
  free (fd_ptr);
}

/* Cadroid: syscall handler modified function */
static void
syscall_handler (struct intr_frame *f)
{
  check_memory (f->esp, sizeof(void*));
  
  uint32_t *ptr = f->esp;
  uint32_t syscall = *ptr;
  uint32_t *result = &f->eax;

  switch (syscall)
  {
    case SYS_HALT:			/* Halt the operating system. */
      syscall_halt();
      break;

    case SYS_EXIT:              	/* Terminate this process. */
      check_memory (ptr+1, sizeof(void*));
      syscall_exit((int)(*(ptr+1)));
      break;

    case SYS_EXEC:              	/* Start another process. */
      check_memory (ptr+1, sizeof(void*));
      *result = syscall_exec((char*)(*(ptr+1)));
      break;

    case SYS_WAIT:              	/* Wait for a child process to die. */
      check_memory (ptr+1, sizeof(void*));
      *result = syscall_wait((tid_t)(*(ptr+1)));
      break;

    case SYS_CREATE:            	/* Create a file. */
      check_memory (ptr+1, 2*sizeof(void*));
      *result = syscall_create((char*)(*(ptr+1)), (unsigned)(*(ptr+2)));
      break;

    case SYS_REMOVE:            	/* Delete a file. */
      check_memory (ptr+1, sizeof(void*));
      *result = syscall_remove((char*)(*(ptr+1)));
      break;

    case SYS_OPEN:              	/* Open a file. */
      check_memory (ptr+1, sizeof(void*));
      *result = syscall_open((char*)(*(ptr+1)));
      break;

    case SYS_FILESIZE:          	/* Obtain a file's size. */
      check_memory (ptr+1, sizeof(void*));
      *result = syscall_filesize((int)(*(ptr+1)));
      break;      

    case SYS_READ:              	/* Read from a file. */
      check_memory (ptr+1, 3*sizeof(void*));
      *result = syscall_read((int)(*(ptr+1)), (void*)(*(ptr+2)), (unsigned)(*(ptr+3)));
      break;

    case SYS_WRITE:             	/* Write to a file. */
      check_memory (ptr+1, 3*sizeof(void*));
      *result = syscall_write((int)(*(ptr+1)), (void*)(*(ptr+2)), (unsigned)(*(ptr+3)));
      break;

    case SYS_SEEK:              	/* Change position in a file. */
      check_memory (ptr+1, sizeof(void*));
      syscall_seek((int)(*(ptr+1)), (unsigned)(*(ptr+2)));
      break;

    case SYS_TELL:              	/* Report current position in a file. */
      check_memory (ptr+1, sizeof(void*));
      *result = syscall_tell((int)(*(ptr+1)));
      break;

    case SYS_CLOSE:             	/* Close a file. */
      check_memory (ptr+1, sizeof(void*));
      syscall_close((int)(*(ptr+1)));
	  break;	  
  }   
}

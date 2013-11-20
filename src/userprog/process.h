#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

/* CADroid: The shared information between child and parent,
  child creates it and parent holds it in the child_list */
struct info_parent
{
  tid_t id;						/* child process id */
  struct thread *thread;				/* child thread pointer */
  int exit_status;					/* exit status of child */
  struct list_elem elem;				/* list elem to hold in parent's list */
  struct lock lock;					/* lock to access this shared struct */
  struct condition cond;				/* condition to signal parent */
};

/* CADroid: File descriptor structure to hold the file pointer,
  fd_id, a list elem to hold in list */
struct file_dscptr
{
  int fd_id;						/* unique file descriptor id */
  struct file *file;					/* file pointer */
  struct list_elem elem;				/* list element to hold in fd_list */
};
  
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/* CADroid: function to create the shared info structure */
bool create_childinfo (struct thread *t);

#endif /* userprog/process.h */

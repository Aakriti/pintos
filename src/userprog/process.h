#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/* Aakriti: filesys lock functions */
void filesys_lock_init(void);
void filesys_lock_acquire(void);
void filesys_lock_release(void);
struct thread * filesys_lock_holder(void);


#endif /* userprog/process.h */

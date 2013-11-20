#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

/* CADroid: called while user prog exiting */
void syscall_close (int fd);

/* CADroid: to lock the filesys while modifying */
struct lock file_lock;

#endif /* userprog/syscall.h */

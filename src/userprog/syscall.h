#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "process.h"

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

void syscall_init (void);
struct file_descriptor *thread_open_file (struct thread *t, 
                                            struct file *f);
struct file_descriptor* get_thread_file (int fd);

#endif /* userprog/syscall.h */ 

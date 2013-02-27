#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "process.h"

void syscall_init (void);
bool check_ptr_valid (const void *ptr);

#endif /* userprog/syscall.h */ 

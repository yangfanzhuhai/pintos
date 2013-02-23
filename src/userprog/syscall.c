#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /*void *esp = f->esp;
  if (esp == NULL || !is_user_vaddr (esp) || 
        pagedir_get_page (active_pd (), esp) == NULL) 
        {
          // Kill process and release resources
          thread_exit ();
        }
  
        
  */     
  printf ("system call!\n");
  thread_exit ();
}

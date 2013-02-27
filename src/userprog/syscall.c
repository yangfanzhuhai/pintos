#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "lib/user/syscall.h"
#include "userprog/pagedir.h"

/* Sys handler prototypes */
static void sys_halt (void);
static void sys_exit (int status);
static pid_t sys_exec (const char *file);
static int sys_wait (pid_t pid);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned length);
static int sys_write (int fd, const void *buffer, unsigned length);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t *esp = f->esp;

  if (!(check_ptr_valid (esp) && check_ptr_valid (esp + 1) 
     && check_ptr_valid (esp + 2) && check_ptr_valid (esp + 3)))
  {
    //Maybe change for sys_exit when implemented
    thread_exit ();
  }
  else
  {
    uint32_t syscall_number = *esp;
    switch (syscall_number)
    {
      case SYS_HALT:
        sys_halt ();
        break;
      case SYS_EXIT:
        sys_exit (*(esp + 1));
        break;
      case SYS_EXEC:
        f->eax = sys_exec ((char *) *(esp + 1));
        break;
      case SYS_WAIT:
        f->eax = sys_wait (*(esp + 1));
        break;
      case SYS_CREATE:
        f->eax = sys_create ((char *) *(esp + 1), *(esp + 2));
        break;
      case SYS_REMOVE:
        f->eax = sys_remove ((char *) *(esp + 1));
        break;
      case SYS_OPEN:
        f->eax = sys_open ((char *) *(esp + 1));
        break;
      case SYS_FILESIZE:
	      f->eax = sys_filesize (*(esp + 1));
	      break;
      case SYS_READ:
        f->eax = sys_read (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;
      case SYS_WRITE:
        f->eax = sys_write (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
        break;
      case SYS_SEEK:
        sys_seek (*(esp + 1), *(esp + 2));
        break;
      case SYS_TELL:
        f->eax = sys_tell (*(esp + 1));
        break;
      case SYS_CLOSE:
        sys_close (*(esp + 1));
        break;
      default:
        break;
    }
  }

}

static void sys_halt (void)
{
}
static void sys_exit (int status)
{
  struct thread *cur = thread_current ();
  if (cur->parent != NULL)
    {
      struct child *child = look_up_child (cur->parent, cur->tid);
      /* Assert is used because a child must be in the parent's
        children list. */
      ASSERT (child != NULL);
      child->exit_status = status;
    }

  cur->own_exit_status = status;
}
static pid_t sys_exec (const char *file)
{
  return 0;
}

static int sys_wait (pid_t pid)
{
  return process_wait ((tid_t)pid); 
}

static bool sys_create (const char *file, unsigned initial_size)
{
  return false;
}
static bool sys_remove (const char *file)
{
  return false;
}
static int sys_open (const char *file)
{
  return 0;
}
static int sys_filesize (int fd)
{
  return 0;
}
static int sys_read (int fd, void *buffer, unsigned length)
{
  return 0;
}
static int sys_write (int fd, const void *buffer, unsigned length)
{
  return 0;
}
static void sys_seek (int fd, unsigned position)
{
}
static unsigned sys_tell (int fd)
{
  return 0;
}
static void sys_close (int fd)
{
}


bool check_ptr_valid (const void *ptr)
{

  //Maybe change for static uint32_t *active_pd (void) in pagedir.c
  uint32_t *pd = thread_current()->pagedir;

  return ptr != NULL && is_user_vaddr (ptr) && 
         pagedir_get_page (pd,ptr) != NULL;
}




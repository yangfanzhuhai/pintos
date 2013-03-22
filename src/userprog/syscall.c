#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/input.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "vm/mmap.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "userprog/exception.h"

#define SYS_IO_STDOUT_BUFFER_SIZE 256
#define SYS_IO_STDOUT_BUFFER_ENABLED true

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
static mapid_t sys_mmap (int fd, void *addr);
static void sys_munmap (mapid_t);

static void syscall_handler (struct intr_frame *);
static bool check_ptr_valid (const void *ptr);
static void exit_on_invalid_ptr (const void *ptr);
static void mm_check_pointer (const void *ptr);

struct lock filesys_lock;
static struct intr_frame *frame;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init (&filesys_lock);
}


static bool 
check_ptr_valid (const void *ptr)
{
  uint32_t *pd = thread_current()->pagedir;
  return ptr != NULL && is_user_vaddr (ptr) && 
          pagedir_get_page (pd, ptr) != NULL;
}

static void 
exit_on_invalid_ptr (const void *ptr)
{
  if (!check_ptr_valid (ptr))
    thread_exit ();
}

static void
mm_check_pointer (const void *ptr)
{
  if (!(ptr != NULL && is_user_vaddr (ptr)))
    thread_exit ();
    
  struct thread *t = thread_current ();
  uint32_t *pd = t->pagedir;
  void *fault_page = pg_round_down (ptr);
  if (pagedir_get_page (pd, ptr) == NULL &&
        page_lookup (t->pages, fault_page))
    {
      if (ptr >= frame->esp - PUSHA_SIZE 
                      && PHYS_BASE - ptr < STACK_LIMIT)
        {
          // Stack growth
          // Obtain a frame for stack growth. 
          uint8_t *kpage = frame_obtain (PAL_USER, fault_page);
          if (kpage == NULL)
            thread_exit ();       
          
          // Add the page to the process's address space. 
          if (!install_page (fault_page, kpage, true))
            {
              frame_release (kpage);
              thread_exit ();
            }
        }
    }
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *esp = f->esp;
  
  frame = f;
  
  exit_on_invalid_ptr (esp);
  exit_on_invalid_ptr (esp + 1);
  exit_on_invalid_ptr (esp + 2);
  exit_on_invalid_ptr (esp + 3);

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
      mm_check_pointer ((void *)*(esp + 1));
      f->eax = sys_exec ((char *) *(esp + 1));
      break;
    case SYS_WAIT:
      f->eax = sys_wait (*(esp + 1));
      break;
    case SYS_CREATE:
      mm_check_pointer ((void *)*(esp + 1));
      f->eax = sys_create ((char *) *(esp + 1), *(esp + 2));
      break;
    case SYS_REMOVE:
      mm_check_pointer ((void *)*(esp + 1));
      f->eax = sys_remove ((char *) *(esp + 1));
      break;
    case SYS_OPEN:
      mm_check_pointer ((void *)*(esp + 1));
      f->eax = sys_open ((char *) *(esp + 1));
      break;
    case SYS_FILESIZE:
      f->eax = sys_filesize (*(esp + 1));
      break;
    case SYS_READ: 
      mm_check_pointer ((void *)*(esp + 2));  
      f->eax = sys_read (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
      break;
    case SYS_WRITE:
      mm_check_pointer ((void *)*(esp + 2));
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
    case SYS_MMAP:
      f->eax = sys_mmap (*(esp + 1), (void *) *(esp + 2));
      break;
    case SYS_MUNMAP:
      sys_munmap (*(esp + 1));
      break;
    default:
      break; 
  }
}

static void sys_halt (void)
{
  shutdown_power_off ();
}

static void sys_exit (int status)
{
  struct thread *cur = thread_current ();
  
  /* Update the child status struct in the parent's children list. */
  if (cur->parent != NULL)
    {
      struct child *child = look_up_child (cur->parent, cur->tid);
      /* Assert is used because a child must be in the parent's
        children list. */
      ASSERT (child != NULL);
      child->exit_status = status;
    }

  cur->own_exit_status = status;
  
  thread_exit();
}

static pid_t 
sys_exec (const char *file)
{
  return (pid_t) process_execute (file);
}

static int 
sys_wait (pid_t pid)
{
  return process_wait ((tid_t)pid); 
}

/* Creates a new file called file initially initial size bytes in size. 
   Returns true if successful, false otherwise. 

   Creating a new file does not open it: 
      Opening the new file is a separate operation which would require a open 
      system call. */
static bool 
sys_create (const char *file, unsigned initial_size)
{
  lock_acquire (&filesys_lock);
  bool return_val = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return return_val;
}

/* Deletes the file called file. Returns true if successful, false otherwise. 
   A file may be removed regardless of whether it is open or closed, and 
   removing an open file does not close it.

   When a file is removed any process which has a file descriptor for that file
   may continue to use that descriptor. This means that they can read and write
   from the file. The file will not have a name, and no other processes will be
   able to open it, but it will continue to exist until all file descriptors
   referring to the file are closed or the machine shuts down. */
static bool 
sys_remove (const char *file)
{
  lock_acquire (&filesys_lock);
  bool return_val = filesys_remove (file);
  lock_release (&filesys_lock);
  return return_val;
}


/* Open file */
static int 
sys_open (const char *fileName)
{
  lock_acquire (&filesys_lock);
  struct file* f = filesys_open (fileName);
  lock_release (&filesys_lock);

  /* File doesn't exist */
  if (f == NULL)
    {
      return -1;
    }

  /* Max file open limit reached */
  if (list_size (&thread_current()->open_files) >= SYS_IO_MAX_FILES)
    {
      return -1;
    }
  
  return (thread_open_file (thread_current(), f))->fd;  
}

/* Creates a file descriptor struct for the current thread based on 
  the given file, and adds it into the open_files list. */
struct file_descriptor *
thread_open_file (struct thread *t, struct file *f)
{
  /* Create file descriptor */
  struct file_descriptor *f_d = (struct file_descriptor*)
  malloc (sizeof (struct file_descriptor));
  f_d->file = f;
  f_d->fd = t->next_fd++;

  // Insert the opened file into the current thread's open file list
  list_push_front (&t->open_files, &f_d->elem);
  
  return f_d;
}


static int 
sys_filesize (int fd)
{
  lock_acquire (&filesys_lock);
  struct file_descriptor *f_d = get_thread_file (fd);

  // fd not found -> file contains nothing
  if (f_d == NULL)
    {
      lock_release (&filesys_lock);
      return -1;
    }
  else
    {
      lock_release (&filesys_lock);
      return file_length (f_d->file);
    }
}



/* System read */
static int 
sys_read (int fd, void *buffer, unsigned length)
{

  int i = 1;

  while ( buffer + PGSIZE * i < buffer + length - 1)
  {
    mm_check_pointer (buffer + PGSIZE * i); 
    i++; 
  }
  mm_check_pointer (buffer + length - 1);

  lock_acquire (&filesys_lock);

  /* If we're reading from STDIN */
  if (fd == STDIN_FILENO)
    {
      int bytesRead = 0;

      /* Read up to "length" bytes from keyboard */
      while (bytesRead < (int)length)
        {
          char readChar = input_getc();
          char* currentChar = (char*)buffer + bytesRead; 

          /* Terminate input if user presses enter */
          if (readChar == '\n')
            {
              *currentChar = '\0';
              break;
            }
          *currentChar = readChar;
          bytesRead++;
        }
    lock_release (&filesys_lock);
    return bytesRead;
  }
  
  /* Reading from normal file */
  else
    {
      struct file_descriptor *f_d = get_thread_file (fd);

      if (f_d == NULL)
        {
          lock_release (&filesys_lock);
          return -1;
        }
      else
        {
          int bytes_read = file_read (f_d->file, buffer, length);
          lock_release (&filesys_lock);
          return bytes_read; 
        }
    }
}


static int 
sys_write (int fd, const void *buffer, unsigned length)
{

  int i = 1;

  while ( buffer + PGSIZE * i < buffer + length - 1)
  {
    mm_check_pointer (buffer + PGSIZE * i); 
    i++; 
  }
  mm_check_pointer (buffer + length - 1);

  lock_acquire (&filesys_lock);

  /* --- --- --- --- --- --- ---*
   * Write to console           *
   * --- --- --- --- --- --- ---*/
  if (fd == STDOUT_FILENO)
    {
      /* Split buffer into sub buffers */
      if (SYS_IO_STDOUT_BUFFER_ENABLED)
        { 
          int i;
          int wholeBufferCount = length / SYS_IO_STDOUT_BUFFER_SIZE;
          int incompleteBufferLength = length % SYS_IO_STDOUT_BUFFER_SIZE;
          
          /* Write whole sub buffers */
          for (i = 0; i < wholeBufferCount; i++)
            {
              int bufferOffset = i * SYS_IO_STDOUT_BUFFER_SIZE;
              putbuf (buffer + bufferOffset, SYS_IO_STDOUT_BUFFER_SIZE);
            }

          /* Incomplete sub buffer to write */
          if (incompleteBufferLength != 0)
            {
              int bufferOffset = wholeBufferCount * SYS_IO_STDOUT_BUFFER_SIZE;
              putbuf (buffer + bufferOffset, incompleteBufferLength);
            }
        }
      /* Push entire buffer as single block */
      else
        {
          putbuf (buffer, length);
        }
      
      /* Will write the length specified */
      lock_release (&filesys_lock);
      return length;
    }

  /* --- --- --- --- --- --- ---*
   * Write to file              *
   * --- --- --- --- --- --- ---*/
  else
    {
      struct file_descriptor *f_d = get_thread_file (fd);

      /* No bytes could be written since file doesn't exist */
      if (f_d == NULL)
        {
          lock_release (&filesys_lock);
          return 0;
        }

      /* File does exist */
      else
        {
          int bytes_written = file_write (f_d->file, buffer, length);
          lock_release (&filesys_lock);
          return bytes_written;
        }
     
    }
}

/* Seek the file to the given position */
static void 
sys_seek (int fd, unsigned position)
{
  lock_acquire (&filesys_lock);
  struct file_descriptor *f_d = get_thread_file (fd);
  file_seek (f_d->file, position);
  lock_release (&filesys_lock);
}


/* Get the seek position of the file */
static unsigned 
sys_tell (int fd)
{
  lock_acquire (&filesys_lock);
  struct file_descriptor *f_d = get_thread_file (fd);
  unsigned tell = file_tell (f_d->file);
  lock_release (&filesys_lock);
  return tell;
}


static void 
sys_close (int fd)
{
  lock_acquire (&filesys_lock);
  struct file_descriptor *f_d = get_thread_file (fd);

  if (f_d != NULL)
    {
      list_remove (&f_d->elem);
      file_close (f_d->file);
      free (f_d);
    }
  lock_release (&filesys_lock);
}

static mapid_t 
sys_mmap (int fd, void *addr)
{
  struct thread *curr = thread_current ();
  lock_acquire(&filesys_lock);
  mapid_t mapid = mmap_add (curr->mappings, fd, addr);
  lock_release(&filesys_lock);
  return mapid;
}

static void 
sys_munmap (mapid_t mapid)
{
  struct thread *curr = thread_current ();
  lock_acquire(&filesys_lock);
  mmap_remove (curr->mappings, mapid);
  lock_release(&filesys_lock);
}

struct file_descriptor* 
get_thread_file (int fd)
{
  /* Iterate list of open files owned by the thread and return thread matching
     the thread we're looking for */
  struct list_elem *e;

  for (e = list_begin (&thread_current()->open_files); 
       e != list_end (&thread_current()->open_files);
       e = list_next (e))
    {
      struct file_descriptor *f_d = list_entry (e, struct file_descriptor,
        elem);

      if (f_d->fd == fd)
        {
          return f_d;
        }
    }

  /* Not found */
  return NULL;
}



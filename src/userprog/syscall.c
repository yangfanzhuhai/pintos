#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "lib/user/syscall.h"
#include "userprog/pagedir.h"
#include "devices/input.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "threads/synch.h"


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

static struct file_descriptor* get_thread_file (int fd);

static void syscall_handler (struct intr_frame *);
static bool check_ptr_valid (const void *ptr);
static void exit_on_invalid_ptr (const void *ptr);

struct lock filesys_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init (filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t *esp = f->esp;
  
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
      exit_on_invalid_ptr ((void *)*(esp + 1));
      f->eax = sys_exec ((char *) *(esp + 1));
      break;
    case SYS_WAIT:
      f->eax = sys_wait (*(esp + 1));
      break;
    case SYS_CREATE:
      exit_on_invalid_ptr ((void *)*(esp + 1));
      f->eax = sys_create ((char *) *(esp + 1), *(esp + 2));
      break;
    case SYS_REMOVE:
      exit_on_invalid_ptr ((void *)*(esp + 1));
      f->eax = sys_remove ((char *) *(esp + 1));
      break;
    case SYS_OPEN:
      exit_on_invalid_ptr ((void *)*(esp + 1));
      f->eax = sys_open ((char *) *(esp + 1));
      break;
    case SYS_FILESIZE:
      f->eax = sys_filesize (*(esp + 1));
      break;
    case SYS_READ:
      exit_on_invalid_ptr ((void *)*(esp + 2));
      f->eax = sys_read (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
      break;
    case SYS_WRITE:
      exit_on_invalid_ptr ((void *)*(esp + 2));
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

static bool 
check_ptr_valid (const void *ptr)
{
  uint32_t *pd = thread_current()->pagedir;

  return ptr != NULL && is_user_vaddr (ptr) &&  
         pagedir_get_page (pd,ptr) != NULL;
}

static void 
exit_on_invalid_ptr (const void *ptr)
{
  if (!check_ptr_valid (ptr))
    {
      thread_exit ();
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
  lock_aquire (filesys_lock);
  bool return_val = filesys_create (file, initial_size);
  lock_release (filesys_lock);
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
  lock_aquire (filesys_lock);
  bool return_val filesys_remove (file);
  lock_release (filesys_lock);
  return return_val;
}


/* Open file */
static int 
sys_open (const char *fileName)
{
  lock_aquire (filesys_lock);
  struct file* f = filesys_open (fileName);
  lock_release (filesys_lock);

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
  struct file_descriptor *f_d = get_thread_file (fd);

  // fd not found -> file contains nothing
  if (f_d == NULL)
    {
      return -1;
    }
  else
    {
      return file_length (f_d->file);
    }
}



/* System read */
static int 
sys_read (int fd, void *buffer, unsigned length)
{
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
    return bytesRead;
  }
  
  /* Reading from normal file */
  else
    {
      struct file_descriptor *f_d = get_thread_file (fd);

      if (f_d == NULL)
        {
          return -1;
        }
      else
        {
          return file_read (f_d->file, buffer, length); 
        }
    }
}


/*
Writes size bytes from buffer to the open file fd. Returns the number of bytes actually
written, which may be less than size if some bytes could not be written.
Writing past end-of-file would normally extend the file, but file growth is not implemented
by the basic file system. The expected behavior is to write as many bytes as possible up to
end-of-file and return the actual number written, or 0 if no bytes could be written at all.
Fd 1 writes to the console. Your code to write to the console should write all of buffer in
one call to putbuf(), at least as long as size is not bigger than a few hundred bytes. (It is
reasonable to break up larger buffers.) Otherwise, lines of text output by different processes
may end up interleaved on the console, confusing both human readers and our grading scripts.
*/
static int 
sys_write (int fd, const void *buffer, unsigned length)
{
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
          return 0;
        }

      /* File does exist */
      else
        {
          return file_write (f_d->file, buffer, length);
        }
     
    }
}

/* Seek the file to the given position */
static void 
sys_seek (int fd, unsigned position)
{
  struct file_descriptor *f_d = get_thread_file (fd);
  file_seek (f_d->file, position);
}


/* Get the seek position of the file */
static unsigned 
sys_tell (int fd)
{
  struct file_descriptor *f_d = get_thread_file (fd);
  return file_tell (f_d->file);
}


static void 
sys_close (int fd)
{
  struct file_descriptor *f_d = get_thread_file (fd);

  if (f_d != NULL)
    {
      list_remove (&f_d->elem);
      file_close (f_d->file);
      free (f_d);
    }
}

static struct file_descriptor* 
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


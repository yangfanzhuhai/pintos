#include "vm/frame.h"
#include "vm/swap.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"

#define FRAME_EVICTION_ALGORITHM 0

static struct lock frame_table_lock;
static struct list frame_table;


void
frame_table_init (void)
{
  lock_init (&frame_table_lock);
  list_init (&frame_table);
}


struct frame_table_entry*
frame_evict_choose_fifo (void)
{
  struct list_elem *back = list_back (&frame_table);
  return list_entry (back, struct frame_table_entry, elem);
}

void*
frame_evict (void* uaddr UNUSED)
{
  // bool pagedir_is_dirty (uint32 t *pd, const void *page )
  // bool pagedir_is_accessed (uint32 t *pd, const void *page )

  // void pagedir_set_dirty (uint32 t *pd, const void *page, bool value )
  // void pagedir_set_accessed (uint32 t *pd, const void *page, bool value )


  /* 1. Choose a frame to evict, using your page replacement algorithm.
        The "accessed" and "dirty" bits in the page table, described below, 
        will come in handy. */
  struct frame_table_entry *fte = NULL;
  switch (FRAME_EVICTION_ALGORITHM)
  {
    /* First in first out */
    case 0:
      fte = frame_evict_choose_fifo ();
  }
  ASSERT (fte != NULL);


  /* 2. Remove references to the frame from any page table that refers to it.
        Unless you have implemented sharing, only a single page should refer to
        a frame at any given time. */
  lock_acquire (&frame_table_lock);
  //list_remove (&fte->elem);
  //palloc_free_page (fte->page);
  lock_release (&frame_table_lock);


  /* 3. If necessary, write the page to the file system or to swap.
        The evicted frame may then be used to store a different page. */
  int index = swap_to_disk (fte->uaddr);
  // Something to do with Luke
  // Move fte->page_address / fte->frame_address to disk / swap


  

  return NULL;
}

/* Given a virtual address (page) find a frame to put the page in and return 
   the physical address of the frame */
void*
frame_obtain (enum palloc_flags flags, void* uaddr)
{
  struct frame_table_entry* fte;

  /* Try and obtain frame in user memory */
  void *kaddr = palloc_get_page (flags);

  /* Successfully obtained frame */
  if (kaddr != NULL)
    {
      /* Create new frame table entry mapping the given page to the allocated
         frame */
      fte = (struct frame_table_entry *) malloc 
                (sizeof (struct frame_table_entry));

      fte->kaddr = kaddr;
      fte->uaddr = uaddr;
      lock_acquire (&frame_table_lock);
      list_push_front (&frame_table, &fte->elem);
      lock_release (&frame_table_lock);
      return fte->kaddr;
    }

  /* Failed to obtain frame */
  else
    {
      /* Perform eviction to release a frame and try allocation again */
      return frame_evict (uaddr);
    }
}


/* Release the frame holding the page specified by uaddr */
void
frame_release (void* uaddr)
{
  lock_acquire (&frame_table_lock);

  struct frame_table_entry *fte = frame_lookup_uaddr (uaddr);
  ASSERT (fte != NULL);

  list_remove (&fte->elem);
  palloc_free_page (&fte->uaddr);

  lock_release (&frame_table_lock);
}

struct frame_table_entry*
frame_lookup_uaddr (void* uaddr)
{
  struct list_elem *e;

  lock_acquire (&frame_table_lock);

  for (e = list_begin (&frame_table);
       e != list_end (&frame_table); 
       e = list_next (e))
    {
      struct frame_table_entry *fte = 
          list_entry (e, struct frame_table_entry, elem);

      if (fte->uaddr == uaddr)
        {
          lock_release (&frame_table_lock);
          return fte;
        }
    }
  return NULL;
}


struct frame_table_entry*
frame_lookup_kaddr (void* kaddr)
{
  struct list_elem *e;

  lock_acquire (&frame_table_lock);

  for (e = list_begin (&frame_table);
       e != list_end (&frame_table); 
       e = list_next (e))
    {
      struct frame_table_entry *fte =
          list_entry (e, struct frame_table_entry, elem);

      if (fte->kaddr == kaddr)
        {
          lock_release (&frame_table_lock);
          return fte;
        }
    }
  return NULL;
}

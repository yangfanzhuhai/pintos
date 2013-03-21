#include "vm/frame.h"

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
  return list_back (&frame_table);
}

void*
frame_evict (void* page_address)
{
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
  lock_aquire (&frame_table_lock);
  list_remove (&f->elem);
  //palloc_free_page (f->page);
  lock_release (&frame_table_lock);


  /* 3. If necessary, write the page to the file system or to swap.
        The evicted frame may then be used to store a different page. */
  // Something to do with Luke
  // Move fte->page_address / fte->frame_address to disk / swap

  /* 4. Recycle
}

/* Given a virtual address (page) find a frame to put the page in and return 
   the physical address of the frame */
void*
frame_obtain (void* page_address)
{
  struct frame_table_entry* fte;

  /* Try and obtain frame in user memory */
  void *frame_address = palloc_get_page (PAL_USER);
  
  

  /* Successfully obtained frame */
  if (frame_address != NULL)
    {
      /* Create new frame table entry mapping the given page to the allocated
         frame */
      fte = (struct frame *) malloc (sizeof (struct frame));
      fte->frame_address = frame_address;
      fte->page_address = page_address;
      lock_aquire (&frame_table_lock);
      list_push_front(&frame_list, &fte->elem);
      lock_release (&frame_table_lock);
    }

  /* Failed to obtain frame */
  else
    {
      /* Perform eviction to release a frame and try allocation again */
      frame_evict ();
    }

  
  return f;
}




#include "vm/frame.h"

#include "threads/synch.h"

static struct lock frame_table_lock;
static struct list frame_table;


void
frame_table_init ()
{
  lock_init (&frame_table_lock);
  list_init (&frame_table);
}

frame*
frame_obtain ()
{
  struct frame* f;
  void *user_page = palloc_get_page (PAL_USER);
  
  lock_aquire (&frame_table_lock);

  if (user_page == NULL)
    {
      /* No free frames - Swap frame */
      /* Some eviction goes here */
      

    }
  else
    {
      /* Create new frame */
      f = (struct frame *) malloc (sizeof (struct frame));
      list_push_back(&frame_list, &f->elem);
    }

  lock_release (&frame_table_lock);
  return f;
}


void
frame_release (struct frame* f)
{
  lock_aquire (&frame_table_lock);
  list_remove (&f->elem);
  /* Do something to clean up page */
  lock_release (&frame_table_lock);
}

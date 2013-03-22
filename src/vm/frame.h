#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/list.h"
#include "threads/palloc.h"

struct frame_table_entry 
{
  struct list_elem elem;
  struct thread *owner;
  void* kaddr;
  void* uaddr;
  bool pin;
};

void frame_table_init (void);
struct frame_table_entry* frame_evict_choose_fifo (void);
struct frame_table_entry* frame_evict_choose_secondchance (void);
void* frame_evict (void* page_address);
void* frame_obtain (enum palloc_flags flags, void* page_address);
void frame_release (void* uaddr);
struct frame_table_entry* frame_lookup_uaddr (void* uaddr);
struct frame_table_entry* frame_lookup_kaddr (void* kaddr);

#endif /* vm/frame.h */

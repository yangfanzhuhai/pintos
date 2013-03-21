#ifndef VM_FRAME_H
#define VM_FRAME_H

struct frame_table_entry 
{
  struct list_elem elem;
  void* frame_address;
  void* page_address;
};

void frame_table_init (void);
struct frame_table_entry* frame_evict_choose_fifo (void);
void* frame_evict (void* page_address);
void* frame_obtain (void* page_address);

#endif /* vm/frame.h */

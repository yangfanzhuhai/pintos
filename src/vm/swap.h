#ifndef SWAP_H
#define SWAP_H

void swaptable_init (void);
int swap_to_disk (const void *uvaddr);
void swap_from_disk (int index, void *uvaddr);
void clear_swap_entry (int index);

#endif

#include "devices/block.h"
#include "threads/vaddr.h"
#include "vm/swap.h"
#include <bitmap.h>
#include <stdbool.h>
#include <stdio.h>
#include "threads/thread.h"

struct block *swap_block;
static struct bitmap *swap_bitmap;

void
swaptable_init ()
{
  swap_block = block_get_role (BLOCK_SWAP);
  if (swap_block == NULL)
    PANIC("Swaptable initialisation failed 1.");

  int swaptable_size = block_size_pages (swap_block);

  swap_bitmap = bitmap_create (swaptable_size);
  if (swap_bitmap == NULL)
    PANIC("Swaptable initialisation failed .");
}

int
swap_to_disk (void *uvaddr)
{
  int index = (int) bitmap_scan_and_flip (swap_bitmap, 0, 1, false);

  if(index == (int) BITMAP_ERROR)
  {
    PANIC ("Swap slot is full. ");
  }

  /* Writes each sector of the block device from uvaddr. 
     block_write states 'Internally synchronizes accesses to block devices, 
     so external per-block device locking is unneeded.'*/
  int i;
  for( i = 0; i < 8; i++)
  {
    block_write (swap_block, index + i, uvaddr + i * BLOCK_SECTOR_SIZE);
  }
  printf( "Process name: %s, swap_index_to: %d \n", thread_current()->name, index);
  return index;
}

void
swap_from_disk (int index, void *uvaddr)
{
  ASSERT( index >=0);

  printf( "Process name: %s, swap_index_from: %d \n", thread_current()->name, index);

  /* Reads each sector of the block device into uvaddr. 
     block_read states 'Internally synchronizes accesses to block devices, 
     so external per-block device locking is unneeded.'*/
  int i;
  for( i = 0; i < 8; i++)
  {
    block_read (swap_block, index + i, uvaddr + i * BLOCK_SECTOR_SIZE);
  }

  clear_swap_entry (index);
}

void
clear_swap_entry (int index)
{
  bitmap_flip (swap_bitmap, index);
}






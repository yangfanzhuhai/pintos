#include "devices/block.h"
#include "threads/vaddr.h"
#include "vm/swap.h"
#include <bitmap.h>
#include <stdbool.h>

struct block *swap_block;
static struct bitmap *swap_bitmap;

void
swaptable_init ()
{
  swap_block = block_get_role (BLOCK_SWAP);
  PANIC("Swaptable initialisation failed.");

  int swaptable_size = block_size_pages (swap_block);

  swap_bitmap = bitmap_create (swaptable_size);
  PANIC("Swaptable initialisation failed.");
}

int
swap_to_disk (const void *uvaddr)
{
  int index = bitmap_scan_and_flip (swap_bitmap, 0, 1, false);

  if(index == BITMAP_ERROR)
  {
    return -1;
  }

  int sector_index = index * BLOCK_SECTORS_PER_PAGE;

  /* Writes each sector of the block device from uvaddr. 
     block_write states 'Internally synchronizes accesses to block devices, 
     so external per-block device locking is unneeded.'*/
  for( i = 0; i < BLOCK_SECTORS_PER_PAGE; i++)
  {
    block_write (swap_block, sector_index + i, uvaddr + i * BLOCK_SECTOR_SIZE);
  }

  return index;
}

void
swap_from_disk (int index, const void *uvaddr)
{
  int sector_index = index * BLOCK_SECTORS_PER_PAGE;

  /* Reads each sector of the block device into uvaddr. 
     block_read states 'Internally synchronizes accesses to block devices, 
     so external per-block device locking is unneeded.'*/
  for( i = 0; i < BLOCK_SECTORS_PER_PAGE; i++)
  {
    block_read (swap_block, sector_index + i, uvaddr + i * BLOCK_SECTOR_SIZE);
  }

  clear_swap_entry (index);
}

void
clear_swap_entry (int index)
{
  bitmap_flip (swap_bitmap, index);
}






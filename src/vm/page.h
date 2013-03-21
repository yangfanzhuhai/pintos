#ifndef PAGE_h
#define PAGE_h

#include <hash.h>
#include "filesys/off_t.h"
#include "threads/malloc.h"

#define FILESYS   1
#define SWAPSLOT  2
#define ALLZERO   3
#define PAGEFRAME 4

/* Supplemental Table Entry. It contains information about where the data
  for a page that faulted might be, and information to find that data. */
struct page
{
  struct hash_elem hash_elem; /* Hash table element. */
  void *addr;                 /* Virtual address. */
  
  int page_location_option;   /* Indicates the location of the data. */

  /* Used when the page data in the file system or is an all zero page. */
  bool writable;              /* True if the page is writable. */

  /* Used when the page data is in the file system. */
  struct file *file;          /* Address of the file to read from. */
  off_t ofs;                  /* Offset for the starting point. */
  size_t page_read_bytes;     /* Number of bytes to be read from file. */
 
  /* Used when the page data is in a swap slot. */
  int swap_index;
};
unsigned page_hash (const struct hash_elem *p_, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux);
//struct hash * pages_init (void);
void pages_init (struct hash *pages);
struct page * page_create (void);
void page_insert (struct hash *pages, struct hash_elem *new);
struct page *page_lookup (struct hash *pages, void *address);
void page_delete (struct hash *pages, void *address);
void pages_destroy (struct hash *pages);

#endif /* vm/page.h */

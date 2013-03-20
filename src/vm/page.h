#ifndef PAGE_h
#define PAGE_h

#include <hash.h>

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


  /* Used when the page data is in the file system. */
  struct file *file;          /* Address of the file to read from. */
  off_t ofs;                  /* Offset for the starting point. */
  size_t page_read_bytes;     /* Number of bytes to be read from file. */
  bool writable;              /* True if the page is writable. */

  /* Used when the page data is in a swap slot. */
  int swap_index;
};

#endif /* vm/page.h */

#ifndef MMAP_h
#define MMAP_h

#include <hash.h>
#include "filesys/file.h"
#include "filesys/filesys.h"

struct mapping
{
  struct hash_elem hash_elem;
  mapid_t mapid;
  void *addr;
  int number_of_pages;
  struct file *file;
};

mapid_t allocate_mapid (void);
mapid_t mmap_add (int fd, void *addr);
void mmap_remove (mapid_t mapid);
void mmmap_clear (void);

#endif /* vm/mmap.h */
  

#ifndef MMAP_h
#define MMAP_h

#include <hash.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"

struct mapping
{
  struct hash_elem hash_elem;
  mapid_t mapid;
  void *addr;
  int number_of_pages;
  struct file *file;
};

struct hash * mappings_init (void);
unsigned mapping_hash (const struct hash_elem *m_, void *aux UNUSED);
bool mapping_less (const struct hash_elem *a_, const struct hash_elem *b_,
                    void *aux UNUSED);
mapid_t allocate_mapid (void);
mapid_t mmap_add (int fd, void *addr);
void mmap_remove (mapid_t mapid);
void mmap_clear (void);

#endif /* vm/mmap.h */
  

#include "vm/mmap.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"


static void free_mapping (struct hash_elem *e);

static struct hash mappings;
static struct lock map_lock;

struct hash *
mappings_init (void)
{
  lock_init(&map_lock);

  if (hash_init (&pages, mapping_hash, mapping_less, NULL))
    return &pages;
  else
    return NULL;
}

unsigned
mapping_hash (const struct hash_elem *m_ void *aux UNUSED)
{
  const struct mapping *mapping = hash_entry (m_, struct mapping, hash_elem);
  return hash_bytes (&mapping->mapid, sizeof mapid);
}

bool
mapping_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct mapping *a = hash_entry (a_, struct mapping, hash_elem);
  const struct mapping *b = hash_entry (b_, struct mapping, hash_elem);
  return a->mapid < b->mapid;
}

mapid_t
allocate_mapid ()
{
  static mapid_t next_mapid = 1;
  mapid_t mapid;

  lock_acquire (&mapid_lock);
  mapid = next_mapid++;
  lock_release (&mapid_lock);

  return mapid; 
}

mapid_t 
mmap_add (int fd, void *addr)
{
  struct thread * current_thread = thread_current();
  
  /* STDIN and STDOUT are not mappable so fails */
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
    return -1;

  /* Must fail is addr is 0 as pintos assumes vaddr 0 is unmapped */
  if ((uintptr_t) addr == 0)
    return -1;

  /* File can not be mapped if addr is not page aligned. */
  if ((uintptr_t) addr % PGSIZE != 0)
    return -1;

  struct file_descriptor *f_d = get_thread_file (fd);

  struct file *file = file_reopen(f_d->file);

  int file_size = file_length (file);    

  /* Can not map file of length 0 */
  if (file_size == 0)
    return -1;

  int number_of_pages = file_size / PGSIZE;

  if (file_size % PGSIZE != 0)
    number_of_pages++;

  /* Check if pages overlaps with pages which are already mapped */
  int i;
  for (i = 0; i < number_of_pages; i++)
  {
    int page_offset = i*PGSIZE;
  
/*
    if (page_lookup (addr + page_offset, current_thread) != NULL)
    {
      file_close (file);
      return -1;
    }
*/ 
  }

  mapid_t mapid = allocate_mapid ();

  struct hash_elem *e = (struct hash_elem*) malloc (sizeof (struct hash_elem));
  struct mapping *m = (struct mapping*) malloc (sizeof (struct mapping);
  m->hash_elem = e;
  m->mapid = mapid
  m->addr = addr;
  m->number_of_pages = number_of_pages;
  m->file = file;

  hash_insert(&mappings, e);

  int bytes_read = 0;
  int bytes_zero = 0;
   for (i = 0; i < number_of_pages; i++)
  {
    int page_offset = i*PGSIZE;
  
    if (file_size >= PGSIZE)
    {
      bytes_read = PGSIZE;
      bytes_zero = 0;
    }
    else
    {
      bytes_read = file_size;
      bytes_zero = PGSIZE - bytes_read;
    }
    
    //page_add (addr + page_offset, bytes_read, bytes_zero, mapid);

    file_size -= bytes_read;
  }

  return mapid; 
}

void 
mmap_remove (mapid_t mapid)
{
  struct mapping m;
  struct hash_elem *e;
  m.mapid = mapid;
  e = hash_delete(&mappings, &p.hash_elem);
  free_mapping(e);
}

void
mmap_clear ()
{
  hash_clear(&mappings, &free_mapping);
}

static void 
free_mapping(struct hash_elem *e)
{
  struct mapping *mapping = hash_entry (e, struct mapping, hash_elem);

  int i;
  for (i = 0, i < mapping->number_of_pages; i++)
  {
    //page_remove (mapping->addr + i*PGSIZE)
  }

  file_close (mapping->file);
  free(e);
  free(mapping);
}







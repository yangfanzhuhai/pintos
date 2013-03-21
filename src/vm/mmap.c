#include "vm/mmap.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <stdio.h>


static void free_mapping (struct hash_elem *e, void* aux UNUSED);

/* Lock to ensure that mapids can be allocated uniquely. */
static struct lock mapid_lock;

unsigned
mapping_hash (const struct hash_elem *m_, void *aux UNUSED)
{
  const struct mapping *mapping = hash_entry (m_, struct mapping, hash_elem);
  return hash_bytes (&mapping->mapid, sizeof (mapid_t));
}

bool
mapping_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct mapping *a = hash_entry (a_, struct mapping, hash_elem);
  const struct mapping *b = hash_entry (b_, struct mapping, hash_elem);
  return a->mapid < b->mapid;
}

/* Following three functions used for hash table behaviour. */
struct hash *
mappings_init (void)
{
  lock_init(&mapid_lock);
  struct hash *mappings = malloc (sizeof (struct hash));
  if (mappings == NULL)
    PANIC ("Failed to intialise mappings");
  if (hash_init (mappings, mapping_hash, mapping_less, NULL))
    return mappings;
  else
    PANIC ("Failed to intialise mappings");
    return NULL;
}

/* Generates unique mapid */
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
mmap_add (struct hash *mappings, int fd, void *addr)
{
  struct thread * current_thread = thread_current();
  
  /* STDIN and STDOUT are not mappable so fails */
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
    return MAP_FAILED;

  /* Must fail is addr is 0 as pintos assumes vaddr 0 is unmapped */
  if ((uintptr_t) addr == 0)
    return MAP_FAILED;

  /* File can not be mapped if addr is not page aligned. */
  if ((uintptr_t) addr % PGSIZE != 0)
    return MAP_FAILED;

  struct file_descriptor *f_d = get_thread_file (fd);

  struct file *file = file_reopen(f_d->file);

  int file_size = file_length (file);    

  /* Can not map file of length 0 */
  if (file_size == 0)
    return MAP_FAILED;

  /* Calculate the number of pages needed to store the file. 
     +1 if not a multiple of PGSIZE */
  int number_of_pages = file_size / PGSIZE;
  if (file_size % PGSIZE != 0)
    number_of_pages++;

  /* Check if pages overlaps with pages which are already mapped */
  int i;
  for (i = 0; i < number_of_pages; i++)
  {
    int page_offset = i*PGSIZE;
  
/*
    if (page_lookup (addr + page_offset, current_thread) != NULL ||
       */
    if(pagedir_get_page(current_thread->pagedir, addr + page_offset))
    {
      file_close (file);
      return MAP_FAILED;
    }
 
  }

  mapid_t mapid = allocate_mapid ();

  /* Create mapping element to be stored in the mappign hash table */
  struct mapping *m = (struct mapping*) malloc (sizeof (struct mapping));
  if (m == NULL)
  {
    file_close (file);
    return MAP_FAILED;
  }

  m->mapid = mapid;
  m->addr = addr;
  m->number_of_pages = number_of_pages;
  m->file = file;

  /* Insert the element in to the mapping hash table */
  hash_insert(mappings, &m->hash_elem);

  /* Add each page to the supplementary page table with necessary details */  
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
mmap_remove (struct hash *mappings, mapid_t mapid)
{
  /* Delete the element from the hash table and free the mapping */
  struct mapping m;
  struct hash_elem *e;
  m.mapid = mapid;
  e = hash_delete(mappings, &m.hash_elem);

  ASSERT(e != NULL);

  free_mapping(e, NULL);
}

void
mmap_clear (struct hash *mappings)
{
  /* Delete each element from the hash table and free their mapping*/
  hash_destroy(mappings, &free_mapping);
}

static void 
free_mapping(struct hash_elem *e, void *aux UNUSED)
{
  struct mapping *mapping = hash_entry (e, struct mapping, hash_elem);
  ASSERT(mapping != NULL);  

  /* Remove each page from the supplementary page table */
  int i;
  for (i = 0; i < mapping->number_of_pages; i++)
  {
    //page_remove (mapping->addr + i*PGSIZE)
  }

  /* Close the file and free the mapping/hash element*/
  file_close (mapping->file);
  free(mapping);
}







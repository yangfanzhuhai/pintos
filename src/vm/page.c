#include "vm/page.h"
#include <hash.h>
#include <debug.h>
#include <stdio.h>

static void page_free (struct hash_elem *e, void *aux);

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->addr, sizeof p->addr);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);
  return a->addr < b->addr;
}

/* Initializes pages as a hash table. Panics the kernel on failure. */
struct hash *
pages_init (void)
{
  struct hash *pages = malloc (sizeof (struct hash));
  if (pages == NULL)
    PANIC ("Fail to allocate memory supplemental page table.");
    
  if (hash_init (pages, page_hash, page_less, NULL))
    return pages;
  else
    PANIC ("Fail to initialize supplemental page table.");    
  return NULL;
}

struct page *
page_create (void)
{
  struct page *p= malloc (sizeof (struct page));
  if (p != NULL)
    p->page_location_option = 0;     /* Default page location option. */
  return p;
}


/* Inserts page new into pages. */
void
page_insert (struct hash *pages, struct hash_elem *new)
{
  hash_insert (pages, new);
}

/* Returns the page containing the given virtual address,
 or a null pointer if no such page exists. */
struct page *
page_lookup (struct hash *pages, void *address)
{
  struct page p;
  struct hash_elem *e;
  p.addr = address;
  e = hash_find (pages, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Deletes the page containing the given virtual address. */
void
page_delete (struct hash *pages, void *address)
{
  struct page p;
  struct hash_elem *e;
  p.addr = address;
  e = hash_delete (pages, &p.hash_elem);
  page_free(e, NULL);
}

/* Frees the page containing the given hash_elem. */
static void 
page_free (struct hash_elem *e, void *aux UNUSED)
{
  struct page *trash_page = e != NULL ? hash_entry (e, struct page, hash_elem) 
                                        : NULL;
  if (trash_page != NULL)
    free(trash_page); 
}

/* Destroys pages and frees the memory. */
void 
pages_destroy (struct hash *pages)
{
  hash_destroy (pages, page_free);
}




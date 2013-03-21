#include "vm/page.h"
#include <hash.h>
#include <debug.h>

static struct hash pages;

/* Initializes pages as a hash table. Panics the kernel on failure. */
struct hash *
pages_init (void)
{
  if (hash_init (&pages, page_hash, page_less, NULL))
    return &pages;
  else
    PANIC ("Fail to initialize supplemental page table.");
}

struct page *
page_create (void)
{
  struct page *p= malloc (sizeof (struct page));
  if (p != NULL)
    p->page_location_option = 0;     /* Default page location option. */
  return p;
}

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

/* Inserts page new into pages. */
void
page_insert (struct hash_elem *new)
{
  hash_insert (&pages, new);
}

/* Returns the page containing the given virtual address,
 or a null pointer if no such page exists. */
struct page *
page_lookup (void *address)
{
  struct page p;
  struct hash_elem *e;
  p.addr = address;
  e = hash_find (&pages, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Deletes the page containing the given virtual address,
 or a null pointer if no such page exists. */
struct page *
page_delete (void *address)
{
  struct page p;
  struct hash_elem *e;
  p.addr = address;
  e = hash_delete (&pages, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}



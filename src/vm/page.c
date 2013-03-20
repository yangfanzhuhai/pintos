#include <hash.h>

static struct hash pages;

/* Initializes pages. Returns a pointer to the hash pages on success, 
 NULL on failure. */
struct hash *
pages_init (void)
{
  if (&pages, page_hash, page_less, NULL)
    return &pages;
  else
    return NULL;
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
page_insert (const struct hash_elem *new)
{
  hash_insert (&pages, new);
}

/* Returns the page containing the given virtual address,
 or a null pointer if no such page exists. */
struct page *
page_lookup (const void *address)
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
page_delete (const void *address)
{
  struct page p;
  struct hash_elem *e;
  p.addr = address;
  e = hash_delete (&pages, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}



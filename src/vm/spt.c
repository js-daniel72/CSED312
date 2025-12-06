#include <hash.h>

#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

#include "vm/spt.h"
#include "filesys/file.h"
void
spt_init (void)
{
  // Nothing to initialize for now
}

// Mandatory functions for hash table operations
unsigned
spt_hash (const struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *spt = hash_entry (e, struct spt_entry, elem);
  return hash_bytes (&spt->uaddr, sizeof spt->uaddr);
}

bool
spt_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct spt_entry *sa = hash_entry (a, struct spt_entry, elem);
  struct spt_entry *sb = hash_entry (b, struct spt_entry, elem);
  return sa->uaddr < sb->uaddr;
}

// Function to add a page to the supplemental page table
struct spt_entry*
spt_add_lazy_page (struct hash *spt, struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  struct spt_entry *new_entry = malloc (sizeof (struct spt_entry));
  if (new_entry == NULL)
    return NULL;

  new_entry->uaddr = upage;
  new_entry->status = PAGE_LAZY;
  new_entry->file = file;
  new_entry->offset = ofs;
  new_entry->read_bytes = read_bytes;
  new_entry->zero_bytes = zero_bytes;
  new_entry->writable = writable;
  new_entry->frame = NULL; // Not loaded yet
  new_entry->swap_index = 0; // Not in swap

  struct hash_elem *existing = hash_find (spt, &new_entry->elem);
  if (existing != NULL)
    {
      free (new_entry);
      return NULL; // Entry already exists
    }

  hash_insert (spt, &new_entry->elem);
  return new_entry;
}

// Must go hand in hand with install_page ()
void
spt_activate (struct spt_entry *entry, struct frame *frame)
{
  entry->status = PAGE_MEMORY;
  entry->frame = frame;
}

void
spt_destroy (struct hash *spt)
{
  struct hash_iterator i;
  hash_first (&i, spt);
  while (hash_next (&i)) {
    struct spt_entry *entry = hash_entry (hash_cur (&i), struct spt_entry, elem);

    // free associated frame
    if (entry->status == PAGE_MEMORY && entry->frame) {
      frame_free (entry->frame);
    }
    // TODO: handle PAGE_MMAP writeback if dirty, PAGE_SWAP release swap slot, etc.

    free (entry);
  }
  hash_destroy (spt, NULL);
}

// uaddr must be page-aligned
struct spt_entry*
spt_lookup (struct hash *spt, void *uaddr)
{
  struct spt_entry temp;
  temp.uaddr = uaddr;
  struct hash_elem *e = hash_find (spt, &temp.elem);
  if (e == NULL)
    return NULL;
  return hash_entry (e, struct spt_entry, elem);
}

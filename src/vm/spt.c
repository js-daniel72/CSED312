#include <hash.h>
#include <stdio.h>

#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/spt.h"
#include "vm/swap.h"
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
  uint8_t *uaddr = pg_round_down (upage);
  if (read_bytes + zero_bytes != PGSIZE)
    return NULL;
  
  struct spt_entry *new_entry = malloc (sizeof *new_entry);
  if (new_entry == NULL)
    return NULL;

  new_entry->uaddr = uaddr;
  new_entry->status = PAGE_LAZY;
  new_entry->file = file;
  new_entry->offset = ofs;
  new_entry->read_bytes = read_bytes;
  new_entry->zero_bytes = zero_bytes;
  new_entry->writable = writable;
  new_entry->frame = NULL;
  new_entry->swap_index = 0;

  // Check for existing entry keyed by uaddr
  struct spt_entry probe;
  probe.uaddr = uaddr;
  struct hash_elem *existing = hash_find (spt, &probe.elem);
  if (existing != NULL) {
    free (new_entry);
    return NULL;
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
spt_destroy_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *entry = hash_entry (e, struct spt_entry, elem);


  // free associated frame table entry
  if (entry->status == PAGE_MEMORY && entry->frame != NULL)
  {
    entry->frame->pinned = true; // prevent eviction during cleanup
    frame_free (entry->frame);
  }
  // TODO: handle PAGE_MMAP writeback if dirty, PAGE_SWAP release swap slot, etc.
  if (entry->status == PAGE_SWAP)
  {
    swap_free (entry->swap_index);
  }

  free (entry);
}



struct spt_entry*
spt_lookup (struct hash *spt, void *uaddr)
{
  struct spt_entry probe;
  probe.uaddr = pg_round_down (uaddr);
  struct hash_elem *e = hash_find (spt, &probe.elem);
  return e ? hash_entry (e, struct spt_entry, elem) : NULL;
}

void
spt_print_all (struct hash *spt)
{
  struct hash_iterator i;
  hash_first (&i, spt);
  printf("Supplemental Page Table Entries:\n");
  while (hash_next (&i)) {
    struct spt_entry *entry = hash_entry (hash_cur (&i), struct spt_entry, elem);
    printf("  UADDR: %p, STATUS: %d, FRAME: %p\n", entry->uaddr, entry->status, entry->frame);
  }
}

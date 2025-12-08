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

static struct frame* load_page (struct spt_entry *spte);

bool add_memory_page (struct hash *spt, void *uaddr, struct frame *frame, bool writable);
bool add_lazy_page (struct hash *spt, struct file *file, off_t ofs, uint8_t *uaddr, uint32_t read_bytes, uint32_t zero_bytes, bool writable, bool mark_mmap);

void
spt_init (void)
{
  // Nothing to initialize for now
}



/* Mandatory functions for hash table operations */
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



// Map a contiguous range of pages lazily.
// Splits total_read_bytes/zero_bytes per page, creates spt entries,
// and optionally marks them as mmap-backed.
bool
spt_initialize_file_as_lazy (struct hash *spt, struct file *file,
                    off_t start_ofs, uint8_t *start_uaddr,
                    size_t total_read_bytes, size_t total_zero_bytes,
                    bool writable, bool mark_mmap)
{
  off_t ofs = start_ofs;
  uint8_t *upage = start_uaddr;
  size_t read_bytes = total_read_bytes;
  size_t zero_bytes = total_zero_bytes;

  while (read_bytes > 0 || zero_bytes > 0)
  {
    uint32_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    uint32_t page_zero_bytes = PGSIZE - page_read_bytes;

    if (!add_lazy_page (spt, file, ofs, upage, page_read_bytes, page_zero_bytes, writable, mark_mmap))
      return false;

    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    ofs += PGSIZE;
    upage += PGSIZE;
  }
  return true;
}

bool
spt_initialize_as_memory (void *upage)
{
  struct thread *t = thread_current ();

  // Get a frame
  struct frame *frame = frame_alloc (upage, PAL_USER | PAL_ZERO);
  if (frame == NULL)
    return false;
  uint8_t *kpage = frame->kaddr;

  // Install it in page directory
  if (!pagedir_install_page (t->pagedir, upage, kpage, true))
  {
    frame_free (frame);
    return false;
  }

  // create a new spt entry
  if (add_memory_page (&t->s_page_table, upage, frame, true) == NULL)
  {
    frame_free (frame);
    return false;
  }
  frame->pinned = false;

  return true;
}



void
spt_memory_to_lazy (struct spt_entry *entry)
{
  // For mmaped pages, write back if dirty
  if (pagedir_is_dirty (entry->frame->owner->pagedir, entry->uaddr))
  {
    filesys_lock_acquire (__func__);
    file_seek (entry->file, entry->offset);
    file_write (entry->file, entry->frame->kaddr, entry->read_bytes);
    filesys_lock_release (__func__);
  }
  
  // Cleanup of page table and frame
  entry->frame = NULL;
  pagedir_clear_page (entry->frame->owner->pagedir, entry->uaddr);
  frame_free (entry->frame);

  entry->status = PAGE_LAZY;
}

void
spt_memory_to_swap (struct spt_entry *entry)
{
  // Cleanup of swap
  struct frame *f = entry->frame;
  size_t swap_index = swap_out (f->kaddr);
  entry->swap_index = swap_index;

  // Cleanup of page table and frame
  entry->frame = NULL;
  pagedir_clear_page (f->owner->pagedir, f->uaddr);
  frame_free (f);

  entry->status = PAGE_SWAP;
}


bool
spt_swap_to_memory (struct spt_entry *entry)
{
  struct frame *frame = frame_alloc (entry->uaddr, PAL_USER);
  if (frame == NULL)
    return false;

  swap_in (entry->swap_index, frame->kaddr);
  if (!pagedir_install_page (thread_current ()->pagedir, entry->uaddr, frame->kaddr, entry->writable))
  {
    frame_free (frame);
    return false;
  }

  frame->pinned = false;
  entry->status = PAGE_MEMORY;
  entry->frame = frame;

  return true;
}

bool
spt_lazy_to_memory (struct spt_entry *entry)
{
  struct frame *frame = load_page (entry);
  if (frame == NULL) {
    return false;
  }
  frame->pinned = false;
  entry->status = PAGE_MEMORY;
  entry->frame = frame;

  return true;
}



void
spt_destroy_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *entry = hash_entry (e, struct spt_entry, elem);


  // free associated frame table entry
  if (entry->status == PAGE_MEMORY && entry->frame != NULL)
  {
    entry->frame->pinned = true; // prevent eviction during cleanup
    pagedir_clear_page (entry->frame->owner->pagedir, entry->uaddr);
    frame_free (entry->frame);
  }
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


/* Helper functions */
// Function to add a page to the supplemental page table
bool
add_lazy_page (struct hash *spt, struct file *file, off_t ofs, uint8_t *uaddr, uint32_t read_bytes, uint32_t zero_bytes, bool writable, bool mark_mmap)
{
  uint8_t *upage = pg_round_down (uaddr);
  if (read_bytes + zero_bytes != PGSIZE)
    return false;
  
  struct spt_entry *new_entry = malloc (sizeof *new_entry);
  if (new_entry == NULL)
    return false;

  new_entry->uaddr = upage;
  new_entry->status = PAGE_LAZY;
  new_entry->file = file;
  new_entry->offset = ofs;
  new_entry->read_bytes = read_bytes;
  new_entry->zero_bytes = zero_bytes;
  new_entry->writable = writable;
  new_entry->frame = NULL;
  new_entry->mmap = mark_mmap;
  new_entry->swap_index = 0;

  // Check for existing entry keyed by uaddr
  struct spt_entry probe;
  probe.uaddr = uaddr;
  struct hash_elem *existing = hash_find (spt, &probe.elem);
  if (existing != NULL) {
    free (new_entry);
    return false;
  }

  hash_insert (spt, &new_entry->elem);
  return true;
}

bool
add_memory_page (struct hash *spt, void *uaddr, struct frame *frame, bool writable)
{
  uint8_t *page_uaddr = pg_round_down (uaddr);
  struct spt_entry *new_entry = malloc (sizeof *new_entry);
  if (new_entry == NULL)
    return NULL;

  new_entry->uaddr = page_uaddr;
  new_entry->status = PAGE_MEMORY;
  new_entry->frame = frame;
  new_entry->writable = writable;
  
  /* These four are unimportant, since memory-init pages are never backed by file */
  new_entry->file = NULL;
  new_entry->offset = 0;
  new_entry->read_bytes = 0;
  new_entry->zero_bytes = 0;
  
  new_entry->mmap = false;
  new_entry->swap_index = -1;

  // Check for existing entry keyed by uaddr
  struct spt_entry probe;
  probe.uaddr = page_uaddr;
  struct hash_elem *existing = hash_find (spt, &probe.elem);
  if (existing != NULL) {
    free (new_entry);
    return NULL;
  }
  hash_insert (spt, &new_entry->elem);
  return new_entry;
}

// This fully initializes frame table entry
static struct frame*
load_page (struct spt_entry *spte)
{
  ASSERT (spte != NULL);
  ASSERT ((spte->read_bytes + spte->zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (spte->uaddr) == 0);
  ASSERT (spte->offset % PGSIZE == 0);
  
  file_seek (spte->file, spte->offset);

  /* Get a page of memory. */
  struct frame* frame = frame_alloc (spte->uaddr, PAL_USER);
  if (frame == NULL)
    return NULL;

  uint8_t *kpage = frame->kaddr;

  /* Load this page. */
  filesys_lock_acquire (__func__);
  int nread = file_read (spte->file, kpage, spte->read_bytes);
  filesys_lock_release (__func__);

  if (nread != (int) spte->read_bytes)
    {
      frame_free (frame);
      return NULL;
    }
  memset (kpage + spte->read_bytes, 0, spte->zero_bytes);

  /* Add the page to the process's address space. */
  if (!pagedir_install_page (thread_current ()->pagedir, spte->uaddr, kpage, spte->writable))
    {
      frame_free (frame);
      return NULL;
    }

  return frame;
}

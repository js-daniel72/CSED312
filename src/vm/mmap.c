#include "vm/mmap.h"
#include "vm/spt.h"
#include "vm/frame.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <debug.h>
#include <stdint.h>

struct mmap_file*
mmap_get_from_mapid (struct list *mmap_list, mapid_t mapping)
{
  struct list_elem *e;
  for (e = list_begin (mmap_list); e != list_end (mmap_list); e = list_next (e))
    {
      struct mmap_file *m = list_entry (e, struct mmap_file, elem);
      if (m->mapping == mapping)
        return m;
    }
  return NULL;
}

static mapid_t next_id = 1;

mapid_t
mmap_get_next_id (void)
{
  return next_id++;
}

// Unmap and flush all pages in m for thread t, then close and remove record.
void
mmap_unmap_and_flush (struct thread *t, struct mmap_file *m)
{
  if (t == NULL || m == NULL) return;

  uint8_t *upage = (uint8_t *) m->addr;

  filesys_lock_acquire (__func__);
  off_t length = file_length (m->file);
  filesys_lock_release (__func__);

  for (size_t offset = 0; offset < (size_t) length; offset += PGSIZE)
  {
    struct spt_entry *spte = spt_lookup (&t->s_page_table, upage + offset);
    if (spte == NULL) continue; // could have been evicted and removed

    if (spte->status == PAGE_MEMORY)
    {
      if (pagedir_is_dirty (t->pagedir, spte->uaddr))
      {
        filesys_lock_acquire (__func__);
        file_seek (m->file, spte->offset);
        file_write (m->file, spte->frame->kaddr, spte->read_bytes);
        filesys_lock_release (__func__);
      }

      struct frame *f = spte->frame;
      if (f != NULL)
      {
        palloc_free_page (f->kaddr);
        frame_free (f);
      }
    }

    pagedir_clear_page (t->pagedir, upage + offset);
    hash_delete (&t->s_page_table, &spte->elem);
    free (spte);
  }

  file_close (m->file);
  list_remove (&m->elem);
  free (m);
}

#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <stdio.h>
#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/spt.h"
#include "userprog/pagedir.h"

// From now on, EVERY allocation of memory must go through frame_alloc(),
// and EVERY deallocation of memory must go through frame_dealloc().
// (A process never calls palloc directly)

struct lock frame_table_lock;
struct list frame_table;

void
frame_init (void)
{
  lock_init (&frame_table_lock);
  list_init (&frame_table);
}

/* Allocates a frame and returns a frame table entry.
   NOTE that uaddr is NOT GUARANTEED TO BE MAPPED YET. (install_page() must be called separately)
*/
struct frame*
frame_alloc (void *uaddr, enum palloc_flags flags)
{
  // frame table entry (MUST BE FREED LATER)
  struct frame *new_frame = malloc (sizeof (struct frame));
  if (new_frame == NULL)
    return NULL;

  new_frame->kaddr = palloc_get_page (flags);
  if (new_frame->kaddr == NULL)
    {
      // This means that memory is full.
      // TODO: Evict a frame here
      free (new_frame);
      PANIC ("Evict not implemented yet");
    }
  
  new_frame->uaddr = uaddr;
  new_frame->owner = thread_current ();
  new_frame->pinned = false;

  lock_acquire (&frame_table_lock);
  list_push_back (&frame_table, &new_frame->elem);
  lock_release (&frame_table_lock);

  return new_frame;
}

// NOTE: pagedir_destroy () frees the actual frame memory pages
// So we only need to free the frame table entry here
void
frame_free (struct frame *frame)
{
  lock_acquire (&frame_table_lock);
  list_remove (&frame->elem);
  lock_release (&frame_table_lock);

  free (frame);
}


struct frame*
find_frame_by_kaddr (void *kaddr)
{
  lock_acquire (&frame_table_lock);
  for (struct list_elem *e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e))
    {
      struct frame *f = list_entry (e, struct frame, elem);
      if (f->kaddr == kaddr)
        {
          lock_release (&frame_table_lock);
          return f;
        }
    }
  lock_release (&frame_table_lock);
  return NULL;
}

void
frame_print_all (void)
{
  lock_acquire (&frame_table_lock);
  printf ("---- Frame Table ----\n");
  for (struct list_elem *e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e))
    {
      struct frame *f = list_entry (e, struct frame, elem);
      printf ("Frame kaddr: %p, uaddr: %p, owner: %s, pinned: %s\n",
              f->kaddr,
              f->uaddr,
              f->owner->name,
              f->pinned ? "true" : "false");
    }
  printf ("---------------------\n");
  lock_release (&frame_table_lock);
}

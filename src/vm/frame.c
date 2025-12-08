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
static struct list_elem *clock_hand;


bool frame_lock_held_by_current_thread (void);
void frame_regrab_lock (bool was_held);
void evict_frame (void);

void
frame_init (void)
{
  lock_init (&frame_table_lock);
  list_init (&frame_table);
  clock_hand = NULL;
}

/* Allocates a NEW frame and returns a frame table entry.
   NOTE that uaddr is NOT GUARANTEED TO BE MAPPED YET. (install_page() must be called separately)
*/
struct frame*
frame_alloc (void *uaddr, enum palloc_flags flags)
{
  // We will hold the lock for the entirety of this function
  // This is to prevent snatching of evicted frame by other processes
  bool was_held = frame_lock_held_by_current_thread ();
  if (!was_held)
    lock_acquire (&frame_table_lock);


  // frame table entry (MUST BE FREED LATER)
  struct frame *new_frame = malloc (sizeof (struct frame));
  if (new_frame == NULL)
    {
      frame_release_lock_if_needed (was_held);
      return NULL;
    }

  new_frame->kaddr = palloc_get_page (flags);
  if (new_frame->kaddr == NULL)
    {
      // This means that memory is full, so we evict a frame
      evict_frame ();
      new_frame->kaddr = palloc_get_page (flags); // This should always work, since we just evicted a frame
      if (new_frame->kaddr == NULL) // This should never happen since we are holding the lock
        {
          printf("SYNCHRONIZATION ERROR >:( >:( >:( >:( >:( >:( >:( >:( >:( >:( \n");
          free (new_frame);
          frame_release_lock_if_needed (was_held);
          return NULL;
        }
    }
  new_frame->uaddr = uaddr;
  new_frame->owner = thread_current ();
  new_frame->pinned = true;



  list_push_back (&frame_table, &new_frame->elem);
  frame_release_lock_if_needed (was_held);

  return new_frame;
}

// NOTE: pagedir_destroy () frees the actual frame memory pages
// So we only need to free the frame table entry here
void
frame_free (struct frame *frame)
{
  bool was_held = frame_lock_held_by_current_thread ();
  if (!was_held)
    lock_acquire (&frame_table_lock);

  palloc_free_page (frame->kaddr);
  if (clock_hand == &frame->elem)
    {
      clock_hand = list_next (clock_hand);
    }
  list_remove (&frame->elem);
  free (frame);
  
  frame_release_lock_if_needed (was_held);
}


struct frame*
find_frame_by_kaddr (void *kaddr)
{
  struct frame *found_frame = NULL;
  bool was_held = frame_lock_held_by_current_thread ();
  if (!was_held)
    lock_acquire (&frame_table_lock);

  for (struct list_elem *e = list_begin (&frame_table); e != list_end (&frame_table); e = list_next (e))
    {
      struct frame *f = list_entry (e, struct frame, elem);
      if (f->kaddr == kaddr)
        {
          found_frame = f;
          break;
        }
    }
  frame_release_lock_if_needed (was_held);
  return found_frame;
}

void
evict_frame (void)
{
  ASSERT (lock_held_by_current_thread (&frame_table_lock));
  // Initialize clock hand if not yet done
  // The check implicitly assumes that frame_table is not empty, but it is okay
  // since it is VERY unlikely for the table to be empty when eviction is needed.
  if (clock_hand == NULL || clock_hand == list_end (&frame_table))
      clock_hand = list_begin (&frame_table);

  while (true)
    {
      struct frame *f = list_entry (clock_hand, struct frame, elem);
      if (!f->pinned)
        {
          // If it was accessed recently, give it a second chance
          if (pagedir_is_accessed (f->owner->pagedir, f->uaddr))
              pagedir_set_accessed (f->owner->pagedir, f->uaddr, false);
          else
            {
              struct spt_entry *spte = spt_lookup (&f->owner->s_page_table, f->uaddr);
              ASSERT (spte != NULL);

              f->pinned = true;

              if (spte->mmap)
                spt_memory_to_lazy (spte);
              else
                spt_memory_to_swap (spte);
              return;
            }
        }
      
      // Advance clock hand (end -> begin, so the hand traverses circularly)
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&frame_table))
        {
          clock_hand = list_begin (&frame_table);
        }
    }
}


void
frame_print_all (void)
{
  bool was_held = frame_lock_held_by_current_thread ();
  if (!was_held)
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
  frame_release_lock_if_needed (was_held);
}



/* Returns true if the current thread is holding the frame table lock. */
bool
frame_lock_held_by_current_thread (void)
{
  return lock_held_by_current_thread (&frame_table_lock);
}

/* Releases the frame table lock if it was not held before the current function. */
void
frame_release_lock_if_needed (bool was_held)
{
  if (!was_held)
    {
      lock_release (&frame_table_lock);
    }
}

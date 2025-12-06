#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include "threads/palloc.h"
#include <list.h>

struct frame
{
  void *kaddr;              // kernel address, or physical address mapped to kernel space
                            // (so PHYS_BASE + physical address, probably? needs confirmation)
  void *uaddr;              // user virtual address
  struct thread *owner;     // process that owns this frame.

  bool pinned;              // should we evict?

  struct list_elem elem;    // for the global frame_table list
};

extern struct lock frame_table_lock;
extern struct list frame_table;

void frame_init (void);
struct frame* frame_alloc (void *uaddr, enum palloc_flags flags);
void frame_free (struct frame *frame);
struct frame* find_frame_by_kaddr (void *kaddr);

#endif

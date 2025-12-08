#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <stdbool.h>
#include "threads/thread.h"
#include "filesys/file.h"

typedef int mapid_t;

struct mmap_file {
  mapid_t mapping;
  struct file *file;
  void *addr;
  struct list_elem elem;
};

mapid_t mmap_get_next_id (void);
struct mmap_file *mmap_get_from_mapid (struct list *mmap_list, mapid_t id);
void mmap_unmap_and_flush (struct thread *t, struct mmap_file *m);

#endif /* VM_MMAP_H */

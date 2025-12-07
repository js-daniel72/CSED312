#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include <stdint.h>

typedef int mapid_t;

struct mmap_file
{
  mapid_t mapping;
  void* addr;
  struct file *file;
  struct list_elem elem;
};

struct mmap_file* mmap_get_from_mapid (struct list *mmap_list, mapid_t mapping);
mapid_t mmap_get_next_id (void);

#endif

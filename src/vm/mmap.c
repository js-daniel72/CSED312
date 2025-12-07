#include "vm/mmap.h"
#include "lib/kernel/list.h"

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

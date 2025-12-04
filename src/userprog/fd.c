#include "userprog/fd.h"
#include "threads/thread.h"
#include <list.h>

struct file_handle *
lookup_handle_by_fd (int fd)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->fd_table); e != list_end (&cur->fd_table); e = list_next (e))
  {
    struct file_handle *handle = list_entry (e, struct file_handle, elem);
    if (handle->fd == fd)
      return handle;
  }
  return NULL;
}

struct file *
fd_to_file (int fd)
{
  if (fd < 1) return NULL;      // 0, 1 are preassigned
  struct file_handle *h = lookup_handle_by_fd (fd);
  return h ? h->file : NULL;
}

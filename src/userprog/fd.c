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

void
fd_table_destroy (struct list *fd_table)
{
  /* Free each file from the fd table */
  struct list_elem *e = list_begin(fd_table);
  while (e != list_end(fd_table))
  {
    struct file_handle *h = list_entry(e, struct file_handle, elem);
    e = list_remove(&h->elem);

    if (h->file != NULL)
      file_close(h->file);

    free(h);
  }
}

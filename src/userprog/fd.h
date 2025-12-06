#ifndef USERPROG_FD_H
#define USERPROG_FD_H

#include <list.h>
#include "filesys/file.h"

struct file_handle {
  int fd;
  struct file *file;
  struct list_elem elem;
};

struct file_handle *lookup_handle_by_fd (int fd);
struct file *fd_to_file (int fd);
void fd_table_destroy (struct list *fd_table);
#endif

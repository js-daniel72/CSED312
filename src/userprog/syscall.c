#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"

#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"

#include "filesys/file.h"
#include "filesys/filesys.h"

#include "lib/kernel/console.h"
#include "devices/shutdown.h"

static struct lock file_lock;  /* Lock for file operations */


struct file_handle {
  int fd;
  struct file *file;
  struct list_elem elem;
};

/* Helper functions that retrieve struct file from fd or file */
struct file_handle *lookup_handle_by_fd (int fd);
struct file_handle *lookup_handle_by_file (const struct file *f);

/* Helper functions that convert fd and file */
struct file *fd_to_file (int fd);
int file_to_fd (const struct file *f);



static void syscall_handler (struct intr_frame *);
void get_user (uint32_t *dst, uint32_t *usrc);
void validate_ptr (void* ptr);      /* IMPORTANT! This function may do exit(-1) */

void sys_exit (int status);
bool sys_create (const char *file, unsigned initial_size);
int sys_open (const char *file);
void sys_close (int fd);
int sys_write (int fd, const void *buffer, unsigned size);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&file_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  validate_ptr(f->esp);

  uint32_t arg1, arg2, arg3;
  uint32_t syscall_number = *((uint32_t *) f->esp);
  
  switch (syscall_number) {
    case SYS_HALT:
    {
      shutdown_power_off();
      break;
    }
    
    case SYS_EXIT:
    {
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      sys_exit(arg1);
      break;
    }
    case SYS_EXEC:
      // TODO: Implement sys_exec
      break;

    case SYS_WAIT:
      // TODO: Implement sys_wait
      break;


    case SYS_CREATE:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      get_user (&arg2, (uint32_t *) f->esp + 2 );
      f->eax = sys_create ((const char*)arg1, arg2);
      break;
    case SYS_REMOVE:
      // TODO: Implement sys_remove
      break;
    case SYS_OPEN:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      f->eax = sys_open ((const char*)arg1);
      break;
    case SYS_FILESIZE:
      // TODO: Implement sys_filesize
      break;
    case SYS_READ:
      // TODO: Implement sys_read
      break;
    case SYS_WRITE:
    {
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      get_user (&arg2, (uint32_t *) f->esp + 2 );
      get_user (&arg3, (uint32_t *) f->esp + 3 );
      f->eax = sys_write ((int)arg1, (const void *)arg2, arg3);
      break;
    }
    case SYS_SEEK:
      // TODO: Implement sys_seek
      break;
    case SYS_TELL:
      // TODO: Implement sys_tell
      break;
    case SYS_CLOSE:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      sys_close ((int)arg1);
      break;

    /* invalid syscall number */
    default:
      sys_exit(-1);
      break;
  }
}






/* Syscall's central functions below here */

void
sys_exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;

  printf ("%s: exit(%d)\n", cur->name, status);

  thread_exit ();
}




int
sys_write (int fd, const void *buffer, unsigned size)
{
  struct thread *cur = thread_current();

  /* fd must be nonnegative */
  if(fd < 0)  return -1;

  /* checks on buffer. This doesn't look right, but calling validate_ptr won't work;
     I don't think we need word-aligned checking.
     will leave as is for now
  */
  if(buffer == NULL || !is_user_vaddr(buffer))
    sys_exit(-1);
  if(size > 0 && !is_user_vaddr((uint8_t *)buffer + size - 1)){
    sys_exit(-1);
  }
  if(fd == 0){
    return 0;  // edge case; not sure how to resolve (0 is supposed to be std *in*)
  }


  /* stdout case */
  if(fd == 1){
    lock_acquire(&file_lock);
    putbuf(buffer, size);
    lock_release(&file_lock);
    return size;
  }
  return -1;

  /* general case */
  /*
  lock_acquire(&file_lock);
  struct file *f = // in fd_table, return open_file of element whose fd matches input
  if(f == NULL)
  {
    lock_release(&file_lock);
    return -1;
  }
  off_t bytes_written = file_write(f, buffer, size);
  lock_release(&file_lock);

  return (int)bytes_written;
  */
}


bool
sys_create (const char* file, unsigned initial_size)
{
  if (file == NULL) sys_exit(-1);
  validate_ptr(file);
  return filesys_create(file, initial_size);
}

int
sys_open (const char *file)
{
  struct thread *cur = thread_current ();
  struct file_handle *fh;
  struct file *f;
  int fd;

  validate_ptr(file);

  /* Weird name */
  if (file == NULL) return -1;

  lock_acquire(&file_lock);
  f = filesys_open (file);
  lock_release(&file_lock);

  /* Open failed */
  if (f == NULL) return -1;

  /* Initializing file_handle, which is an element of fd_table */
  /* Not sure if malloc is the way to go.... */
  fh = malloc (sizeof *fh);
  if (fh == NULL)
  {
    file_close (f);
    return -1;
  }

  /* Make a new fd_table entry and push to the list */
  fd = cur->next_fd++;
  fh->fd = fd;
  fh->file = f;
  list_push_back (&cur->fd_table, &fh->elem);

  return fd;
}

void
sys_close (int fd)
{
  /* Must be a valid fd */
  if (fd < 1) return;

  /* Retrieve handle from fd, and close it */
  struct file *file = fd_to_file (fd);
  
  if (file == NULL) 
  {
    return;
  }
  lock_acquire (&file_lock);
  file_close (file);
  lock_release (&file_lock);

  /* Remove from fd_table */
  struct file_handle *handle = lookup_handle_by_fd (fd);
  if (handle == NULL) return;
  list_remove (&handle->elem);
}








/* ----- START OF fd_table helper functions ----- */
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


struct file_handle *
lookup_handle_by_file (const struct file *f)
{
  if (f == NULL) return NULL;

  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->fd_table); e != list_end (&cur->fd_table); e = list_next (e))
  {
    struct file_handle *handle = list_entry (e, struct file_handle, elem);
    if (handle->file == f)
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

int
file_to_fd (const struct file *f)
{
  struct file_handle *h = lookup_handle_by_file (f);
  return h ? h->fd : -1;
}
/* ----- END OF fd_table helper functions ----- */





/* EXITS when ptr is invalid, and continues if valid */
void
validate_ptr (void* ptr)
{
  struct thread *cur = thread_current ();

  // Validations 1: ptr in user space, and in an allocated page
  if (!is_user_vaddr (ptr) || pagedir_get_page(cur->pagedir, ptr) == NULL)
    sys_exit(-1);

  // Validations 2: ptr+3 still in user space and in allocated page
  uint8_t* ptr_end = (uint8_t*) ptr + 3;
  if (!is_user_vaddr (ptr_end) || pagedir_get_page(cur->pagedir, ptr_end) == NULL)
    sys_exit(-1);
}

/* Helper function to read arguments from stack */
void
get_user (uint32_t *dst, uint32_t *usrc)
{
  validate_ptr (usrc);  
  *dst = *usrc;
}

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
#include "devices/input.h"

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
pid_t sys_exec (const char *cmd_line);
int sys_wait (pid_t pid);

bool sys_create (const char *file, unsigned initial_size);
int sys_open (const char *file);
void sys_close (int fd);
int sys_filesize (int fd);
int sys_write (int fd, const void *buffer, unsigned size);
int sys_read (int fd, void *buffer, unsigned size);
void sys_seek (int fd, unsigned position);

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
      shutdown_power_off();
      break;
    case SYS_EXIT:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      sys_exit(arg1);
      break;
    case SYS_EXEC:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      f->eax = sys_exec ((const char *)arg1);
      break;
    case SYS_WAIT:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      f->eax = sys_wait ((pid_t)arg1);
      break;


    case SYS_CREATE:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      get_user (&arg2, (uint32_t *) f->esp + 2 );
      f->eax = sys_create ((const char*)arg1, arg2);
      break;
    case SYS_REMOVE:
      printf("Is Remove reached? \n");
      // TODO: Implement sys_remove
      break;
    case SYS_OPEN:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      f->eax = sys_open ((const char*)arg1);
      break;
    case SYS_FILESIZE:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      f->eax = sys_filesize ((int)arg1);
      break;
    case SYS_READ:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      get_user (&arg2, (uint32_t *) f->esp + 2 );
      get_user (&arg3, (uint32_t *) f->esp + 3 );
      f->eax = sys_read ((int)arg1, (void *)arg2, arg3);
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
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      get_user (&arg2, (uint32_t *) f->esp + 2 );
      sys_seek ((int)arg1, (unsigned)arg2);
      break;
    case SYS_TELL:
      printf("Is Tell reached? \n");
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

  // Zombie process
  if(cur->executable != NULL)
  {  
    file_allow_write (cur->executable);
    file_close (cur->executable);
    // printf("allowed writes again from %s \n", cur->name);
    cur->executable = NULL;
  }
  sema_up(&cur->wait_sema);
  sema_down(&cur->zombie_sema);

  thread_exit ();
}

pid_t
sys_exec (const char *cmd_line)
{
  validate_ptr(cmd_line);

  // Create new child process
  tid_t child_tid = process_execute(cmd_line);
  if (child_tid == TID_ERROR) return -1;

  // Wait until child load finishes (thread_by_tid may be buggy)
  struct thread *cur = thread_current();
  struct thread *child = thread_by_tid(child_tid);
  sema_down(&child->load_sema);

  // If child loaded, push it to parent's child list and return tid
  if(child->load_success) return (pid_t) child_tid;
  list_remove (&child->child_elem);
  sema_up(&child->zombie_sema);
  return -1;
}

int sys_wait (pid_t pid)
{
  return process_wait((tid_t) pid);
}

int
sys_write (int fd, const void *buffer, unsigned size)
{
  /* fd must be nonnegative */
  if(fd < 0)  return -1;

  /* Not sure about this check.. */
  validate_ptr(buffer);

  /* stdout case */
  if(fd == 1){
    lock_acquire(&file_lock);
    putbuf(buffer, size);
    lock_release(&file_lock);
    return size;
  }

  /* general case */
  struct file *file = fd_to_file (fd);  
  if (file == NULL) return (-1);

  lock_acquire(&file_lock);  
  off_t bytes_written = file_write(file, buffer, size);  
  lock_release(&file_lock);
  
  return (int)bytes_written;
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
    lock_acquire(&file_lock);
    file_close (f);
    lock_release(&file_lock);
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
  if (file == NULL) return;

  lock_acquire (&file_lock);
  file_close (file);
  lock_release (&file_lock);

  /* Remove from fd_table */
  struct file_handle *handle = lookup_handle_by_fd (fd);
  if (handle == NULL) return;
  list_remove (&handle->elem);
}

int
sys_read (int fd, void *buffer, unsigned size)
{
  /* This needn't be word aligned probably */
  validate_ptr(buffer);

  /* STDIN case */
  if (fd == 0)
  {
    lock_acquire (&file_lock);
    for (int i = 0; i < (int) size; i++)
    {
      ((char*) buffer)[i] = input_getc ();
    }
    lock_release (&file_lock);
    return size;
  }

  /* General case */
  /* (STDOUT is definitely not in the fd_table so that case is handled here) */
  struct file *file = fd_to_file (fd);  
  if (file == NULL) return (-1);

  lock_acquire (&file_lock);
  int length = file_read(file, buffer, size);
  lock_release (&file_lock);

  return length;
}

int
sys_filesize (int fd)
{
  int length;
  if (fd < 1) return -1;
  struct file *file = fd_to_file(fd);
  
  lock_acquire (&file_lock);
  length = file_length (file);
  lock_release (&file_lock);

  return length;
}

void
sys_seek (int fd, unsigned position)
{
  // Don't do anything for stdout and stdin
  if (fd < 1) return;

  struct file* file = fd_to_file(fd);

  lock_acquire(&file_lock);
  file_seek (file, position);
  lock_release(&file_lock);
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

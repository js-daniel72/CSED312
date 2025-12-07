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
#include "userprog/fd.h"

#include "filesys/file.h"
#include "filesys/filesys.h"

#include "lib/kernel/console.h"

#include "devices/shutdown.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
void get_user (uint32_t *dst, uint32_t *usrc);
void validate_ptr (void* ptr, int length);      /* IMPORTANT! This function may do exit(-1) */

void sys_exit (int status);
tid_t sys_exec (const char *cmd_line);
int sys_wait (tid_t tid);

bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
void sys_close (int fd);
int sys_filesize (int fd);
unsigned sys_tell (int fd);
int sys_write (int fd, const void *buffer, unsigned size);
int sys_read (int fd, void *buffer, unsigned size);
void sys_seek (int fd, unsigned position);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  validate_ptr(f->esp, 4);
  thread_current ()->esp = f->esp;

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
      f->eax = sys_wait ((tid_t)arg1);
      break;


    case SYS_CREATE:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      get_user (&arg2, (uint32_t *) f->esp + 2 );
      f->eax = sys_create ((const char*)arg1, arg2);
      break;
    case SYS_REMOVE:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      f->eax = sys_remove ((const char*)arg1);
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
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      f->eax = sys_tell ((int)arg1);
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
  process_cleanup (status);
}

tid_t
sys_exec (const char *cmd_line)
{
  validate_ptr(cmd_line, 4);

  // Create new child process
  tid_t child_tid = process_execute(cmd_line);
  if (child_tid == TID_ERROR) return -1;

  // Wait until child load finishes (thread_by_tid may be buggy)
  struct thread *child = thread_by_tid(child_tid);
  sema_down(&child->load_sema);

  // If child failed to load, remove it from child list
  // NOTE: adding to child list happens in process_execute(), right after the call to thread_create()
  if(child->load_success) return child_tid;
  list_remove (&child->child_elem);
  sema_up(&child->zombie_sema);
  return -1;
}

int
sys_wait (tid_t tid)
{
  return process_wait(tid);
}




int
sys_write (int fd, const void *buffer, unsigned size)
{
  /* fd must be nonnegative */
  if(fd < 0)  return -1;

  validate_ptr(buffer, size);

  /* stdout case */
  if(fd == 1){
    filesys_lock_acquire (__func__);
    putbuf(buffer, size);
    filesys_lock_release (__func__);
    return size;
  }

  /* general case */
  struct file *file = fd_to_file (fd);  
  if (file == NULL) return (-1);

  filesys_lock_acquire (__func__);  
  off_t bytes_written = file_write(file, buffer, size);  
  filesys_lock_release (__func__);
  
  return (int)bytes_written;
}

bool
sys_create (const char* file, unsigned initial_size)
{
  validate_ptr(file, 4);
  if (file == NULL) sys_exit(-1);

  filesys_lock_acquire (__func__);
  bool success = filesys_create(file, initial_size);
  filesys_lock_release (__func__);

  return success;
}

int
sys_open (const char *file)
{
  struct thread *cur = thread_current ();
  struct file_handle *fh;
  struct file *f;
  int fd;

  validate_ptr(file, 4);
  if (file == NULL) sys_exit(-1);
  
  filesys_lock_acquire (__func__);
  f = filesys_open (file);
  filesys_lock_release (__func__);

  /* Open failed */
  if (f == NULL) return -1;

  /* Initializing file_handle, which is an element of fd_table */
  fh = malloc (sizeof *fh);
  if (fh == NULL)
  {
    filesys_lock_acquire (__func__);
    file_close (f);
    filesys_lock_release (__func__);
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

  filesys_lock_acquire (__func__);
  file_close (file);
  filesys_lock_release (__func__);

  /* Remove from fd_table */
  struct file_handle *handle = lookup_handle_by_fd (fd);
  if (handle == NULL) return;
  
  list_remove (&handle->elem);
  free(handle);
}

int
sys_read (int fd, void *buffer, unsigned size)
{
  validate_ptr(buffer, size);

  /* STDIN case */
  if (fd == 0)
  {
    filesys_lock_acquire (__func__);
    for (int i = 0; i < (int) size; i++)
    {
      ((char*) buffer)[i] = input_getc ();
    }
    filesys_lock_release (__func__);
    return size;
  }

  /* General case */
  /* (STDOUT is definitely not in the fd_table so that case is handled here) */
  struct file *file = fd_to_file (fd);  
  if (file == NULL) return (-1);

  filesys_lock_acquire (__func__);
  int length = file_read(file, buffer, size);
  filesys_lock_release (__func__);

  return length;
}

int
sys_filesize (int fd)
{
  int length;
  if (fd < 1) return -1;
  struct file *file = fd_to_file(fd);
  
  filesys_lock_acquire (__func__);
  length = file_length (file);
  filesys_lock_release (__func__);

  return length;
}

void
sys_seek (int fd, unsigned position)
{
  // Don't do anything for stdout and stdin
  if (fd < 1) return;

  struct file* file = fd_to_file(fd);
  if (file == NULL) return;
  
  filesys_lock_acquire (__func__);
  file_seek (file, position);
  filesys_lock_release (__func__);
}

unsigned
sys_tell (int fd)
{
  off_t pos = 0;
  if (fd < 1) return;

  struct file *file = fd_to_file(fd);
  if (file == NULL) return -1;

  filesys_lock_acquire (__func__);
  pos = file_tell (file);
  filesys_lock_release (__func__);
  
  return pos;
}

bool
sys_remove (const char *file)
{
  validate_ptr (file, 4);
  bool success = false;

  filesys_lock_acquire (__func__);
  success = filesys_remove (file);
  filesys_lock_release (__func__);

  return success;
}


void touch_ptr (uint8_t *uaddr);


/* EXITS when ptr is invalid, and continues if valid */
void
validate_ptr (void* ptr, int length)
{
  // For VM project, force a page fault for invalid accesses that are in user space
  #ifdef VM
  struct thread *cur = thread_current ();

  // Validations 1: ptr in user space
  uint8_t *ptr_end = (uint8_t *) ptr + length - 1;
  if (!is_user_vaddr (ptr) || !is_user_vaddr (ptr_end))
    sys_exit (-1);

  // Validations 2: touch each page that ptr spans
  // THIS IS TO CAUSE PAGE FAULT OUTSIDE OF SYSCALLS!!! Prevents nested filesys locks
  // Must touch in a page granularity manner
  for (uint8_t *page = pg_round_down(ptr); page <= pg_round_down(ptr_end); page += PGSIZE)
    touch_ptr(page);

  #else
  struct thread *cur = thread_current ();

  // Validations 1: ptr in user space, and in an allocated page
  if (!is_user_vaddr (ptr) || pagedir_get_page(cur->pagedir, ptr) == NULL)
    sys_exit(-1);

  // Validations 2: ptr+length-1 still in user space and in allocated page
  uint8_t* ptr_end = (uint8_t*) ptr + length - 1;
  if (!is_user_vaddr (ptr_end) || pagedir_get_page(cur->pagedir, ptr_end) == NULL)
    sys_exit(-1);
  #endif
}

#ifdef VM
void
touch_ptr (uint8_t *uaddr)
{
  int result;
  asm volatile ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
}
#endif

/* Helper function to read arguments from stack */
void
get_user (uint32_t *dst, uint32_t *usrc)
{
  validate_ptr (usrc, 4);
  *dst = *usrc;
}

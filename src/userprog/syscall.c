#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include <debug.h>

#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "userprog/fd.h"

#include "vm/mmap.h"
#include "vm/spt.h"
#include "vm/frame.h"

#include "filesys/file.h"
#include "filesys/filesys.h"

#include "lib/kernel/console.h"
#include "lib/kernel/hash.h"

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

#ifdef VM
mapid_t sys_mmap (int fd, void *addr);
void sys_munmap (mapid_t mapping);
#endif

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  validate_ptr(f->esp, 4);
  
  #ifdef VM
  thread_current ()->esp = f->esp;
  #endif

  uint32_t arg1, arg2, arg3;
  uint32_t syscall_number = *((uint32_t *) f->esp);
  
  // printf ("Syscall: %d\n", syscall_number);
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

#ifdef VM
    case SYS_MMAP:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      get_user (&arg2, (uint32_t *) f->esp + 2 );
      f->eax = sys_mmap ((int)arg1, (void *)arg2);
      break;
    case SYS_MUNMAP:
      get_user (&arg1, (uint32_t *) f->esp + 1 );
      sys_munmap ((mapid_t)arg1);
      break;
#endif
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

#ifdef VM
mapid_t
sys_mmap (int fd, void *addr)
{
  struct thread *t = thread_current ();


  /* Start of various checks */

  // 1. Check for invalid fd, null address, or non-page-aligned address.
  if (fd < 2 || addr == NULL || pg_ofs(addr) != 0)
    return -1;

  // 2. Get file from fd (must exist).
  struct file *file = fd_to_file (fd);
  if (file == NULL)
    return -1;

  // 3. Check file length (using original file).
  filesys_lock_acquire (__func__);
  off_t length = file_length (file);
  filesys_lock_release (__func__);
  if (length == 0)
    return -1;

  // 4. Check for overlap with existing pages in SPT over the entire range.
  for (off_t offset = 0; offset < length; offset += PGSIZE)
  {
    if (spt_lookup (&t->s_page_table, addr + offset) != NULL)
      return -1;
  }

  /* End of various checks */


  // Reopen the file and use its pointer (to be independent of original file's close etc)
  struct file *reopened_file = file_reopen (file);
  if (reopened_file == NULL)
    return -1;


  // Create a new mmap_file struct
  struct mmap_file *mmap = malloc (sizeof *mmap);
  if (mmap == NULL)
  {
    file_close (reopened_file);
    return -1;
  }
  mmap->mapping = mmap_get_next_id ();
  mmap->file = reopened_file;
  mmap->addr = addr;
  list_push_back (&t->mmap_list, &mmap->elem);


  // Lazy-load the pages
  off_t ofs = 0;
  uint8_t *upage = (uint8_t *) addr;
  size_t read_bytes = length;
  size_t zero_bytes = (PGSIZE - (length % PGSIZE)) % PGSIZE;
  if (!spt_map_lazy_range (&t->s_page_table, reopened_file, ofs, upage, read_bytes, zero_bytes, true, true))
  {
    list_remove (&mmap->elem);
    file_close (reopened_file);
    free (mmap);
    return -1;
  }

  return mmap->mapping;
}

void
sys_munmap (mapid_t mapping)
{
  struct thread *t = thread_current ();
  struct mmap_file *mmap = mmap_get_from_mapid (&t->mmap_list, mapping);
  if (mmap == NULL) return;

  mmap_unmap_and_flush (t, mmap);
}
#endif



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
  // Turns out this is not needed, but being safe is good
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

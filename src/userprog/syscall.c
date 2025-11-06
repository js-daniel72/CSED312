#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "lib/kernel/console.h"
#include "threads/synch.h"

static struct lock file_lock;  /* Lock for file operations */

static void syscall_handler (struct intr_frame *);

static bool
get_user (uint32_t *dst, const uint32_t *usrc)
{
  /* Validate the pointer is in user space and not null */
  if (usrc == NULL || !is_user_vaddr (usrc))
    return false;
  
  /* Ensure the last byte of the 4-byte value is also in user space */
  uint8_t *last_byte = (uint8_t *) usrc + sizeof (uint32_t) - 1;
  if (!is_user_vaddr (last_byte))
    return false;
  
  /* Read the value */
  *dst = *usrc;
  return true;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&file_lock);
}

void
sys_exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;

  printf ("%s: exit(%d)\n", cur->name, status);

  process_exit ();
  thread_exit ();
}

int
sys_write (int fd, const void *buffer, unsigned size)
{
  // TODO: Check if fd, buffer, and size are valid
  if(fd < 0 || fd > 127)
    return -1;

  if(buffer == NULL || !is_user_vaddr(buffer))
    sys_exit(-1);
  if(size > 0 && !is_user_vaddr((uint8_t *)buffer + size - 1)){
    sys_exit(-1);
  }
  if(fd == 0){
    return 0;  // write을 불러놓고 stdin을 하려고 하면 0? -1? 
  }


  if(fd == 1){
    // TODO: writes to the console using putbuf
    lock_acquire(&file_lock);  // putbuf에 lock이 있긴 함.. 필요 없을지도?
    putbuf(buffer, size);
    lock_release(&file_lock);
    return size;
  }

  // TODO: Write to file at fd position
  struct thread *cur = thread_current();
  lock_acquire(&file_lock);
  struct file *f = cur->fd_table[fd];
  if(f == NULL){
    lock_release(&file_lock);
    return -1;
  }

  off_t bytes_written = file_write(f, buffer, size);
  lock_release(&file_lock);

  return (int)bytes_written;
}

static void
syscall_handler (struct intr_frame *f) 
{
  // TODO: Check if pointer f is valid
  if (f == NULL)
    sys_exit(-1);

  /* Validate that the user stack pointer is in user space */
  if (!is_user_vaddr (f->esp) || f->esp == NULL)
    sys_exit(-1);

  /* Get the syscall number from the user stack */
  uint32_t syscall_number;
  if (!get_user (&syscall_number, (uint32_t *) f->esp))
    sys_exit(-1);

  switch (syscall_number) {
    // syscall related to user process manipulation
    case SYS_HALT:
      // TODO: Implement sys_halt
      break;

    case SYS_EXIT:
      // Get the exit status (first argument) from the stack 
      uint32_t status;
      if(get_user (&status, (uint32_t *) f->esp + 1)){
        sys_exit(status);
      }
      else{
        sys_exit(-1);
      }
      break;

    case SYS_EXEC:
      // TODO: Implement sys_exec
      break;

    case SYS_WAIT:
      // TODO: Implement sys_wait
      break;


    // syscall related to file manipulation
    case SYS_CREATE:
      // TODO: Implement sys_create
      break;
    case SYS_REMOVE:
      // TODO: Implement sys_remove
      break;
    case SYS_OPEN:
      // TODO: Implement sys_open
      break;
    case SYS_FILESIZE:
      // TODO: Implement sys_filesize
      break;
    case SYS_READ:
      // TODO: Implement sys_read
      break;
    case SYS_WRITE:
      uint32_t u_fd, u_buffer, u_size;
      if (!get_user (&u_fd, (uint32_t *) f->esp + 1) ||
          !get_user (&u_buffer, (uint32_t *) f->esp + 2) ||
          !get_user (&u_size, (uint32_t *) f->esp + 3))
        {
          sys_exit (-1);
        }

      int fd = (int) u_fd;
      void* buffer = (void*) u_buffer;
      unsigned  size = (unsigned) u_size;

      f->eax = sys_write (fd, buffer, size);
      break;

    case SYS_SEEK:
      // TODO: Implement sys_seek
      break;
    case SYS_TELL:
      // TODO: Implement sys_tell
      break;
    case SYS_CLOSE:
      // TODO: Implement sys_close
      break;

    default:
      /* Invalid syscall number */
      thread_exit ();
      break;
  }
}

#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

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
}

void
sys_exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  process_exit ();
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f) 
{
  // TODO: Check if pointer f is valid
  if (f == NULL)
    thread_exit ();

  /* Validate that the user stack pointer is in user space */
  if (!is_user_vaddr (f->esp) || f->esp == NULL)
    thread_exit ();

  /* Get the syscall number from the user stack */
  uint32_t syscall_number;
  if (!get_user (&syscall_number, (uint32_t *) f->esp))
    thread_exit ();

  switch (syscall_number) {
    // syscall related to user process manipulation
    case SYS_HALT:
      // TODO: Implement sys_halt
      break;

    case SYS_EXIT:
      // Get the exit status (first argument) from the stack 
      uint32_t status;
      get_user (&status, (uint32_t *) f->esp + 1);
      sys_exit (status);
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
      // TODO: Implement sys_write
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

  /* If we reach here, the syscall handler didn't handle the syscall properly */
  thread_exit ();
}

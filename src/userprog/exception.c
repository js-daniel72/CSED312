#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "threads/vaddr.h"
#include "debug.h"
#include "userprog/pagedir.h"
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "userprog/process.h"

#include "vm/frame.h"
#include "vm/spt.h"

#include "filesys/filesys.h"
#include "filesys/file.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

static struct frame* load_page (struct spt_entry *spte);
static bool install_page (void *upage, void *kpage, bool writable);
/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);

      // This doesn't return
      process_cleanup (-1);
      

   case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  #ifdef VM
  // Handle page fault handling with virtual memory
  {
    bool lock_was_held = filesys_lock_held_by_current_thread ();
    if (lock_was_held)
    {
      filesys_lock_release (__func__);
    }
    struct thread *t = thread_current ();
    void *upage = pg_round_down (fault_addr);

    // Rights violation: no lazy load possible.
    if (!not_present)
      process_cleanup (-1);

    void *esp = user ? f->esp : t->esp;
    // Lookup the supplemental page table entry for the faulting address.
    struct spt_entry *spte = spt_lookup (&t->s_page_table, upage);
    
    // If no spte, there may be stack growth. Handle this case here
    if (spte == NULL) {
      if ((PHYS_BASE - MAX_STACK_SIZE) <= fault_addr && 
           fault_addr < PHYS_BASE &&
           esp - 32 <= fault_addr)
      {
        // Make frame
        struct frame *frame = frame_alloc (upage, PAL_USER);
        if (frame == NULL) {
          process_cleanup (-1);
        }
        memset (frame->kaddr, 0, PGSIZE);
        
        
        // Link frame and vm address
        if (!install_page (upage, frame->kaddr, true))
        {
          frame_free (frame);
          process_cleanup (-1);
        }
        // Make spt entry and activate
        spte = spt_add_lazy_page (&t->s_page_table, NULL, 0, upage, 0, PGSIZE, true);
        spt_activate (spte, frame);
        frame->pinned = false;
        if (lock_was_held)
        {
          filesys_lock_acquire (__func__);
        }
      return;
      }
      else
      {
        process_cleanup (-1);
      }
    }

    struct frame *frame = NULL;

    switch (spte->status)
    {
      // Already mapped but got not-present, so treat as error.
      case PAGE_MEMORY:
        process_cleanup (-1);

      // Lazy load using load_page ().
      case PAGE_LAZY:
        frame = load_page (spte);
        if (frame == NULL) {
          process_cleanup (-1);
        }
        break;

      case PAGE_SWAP:
        // Allocate frame. If my code is right, this should never fail since we are evicting if necessary inside frame_alloc ()
        frame = frame_alloc (spte->uaddr, PAL_USER);
        if (frame == NULL)        // This should never happen in theory
          process_cleanup (-1);

        swap_in (spte->swap_index, frame->kaddr); // This shouldn't have eviction issues since we just allocated a frame
        if (!install_page (spte->uaddr, frame->kaddr, spte->writable))
        {
          frame_free (frame);
          process_cleanup (-1);
        }
        break;
      default:
        process_cleanup (-1);
    }
    frame->pinned = false;
    spt_activate (spte, frame);
    if (lock_was_held)
    {
      filesys_lock_acquire (__func__);
    }
    return;
  }
   #else 
  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */
  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  kill (f);
  #endif
}




// This fully initializes frame table entry
static struct frame*
load_page (struct spt_entry *spte)
{
  ASSERT (spte != NULL);
  ASSERT ((spte->read_bytes + spte->zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (spte->uaddr) == 0);
  ASSERT (spte->offset % PGSIZE == 0);
  
  file_seek (spte->file, spte->offset);

  /* Get a page of memory. */
  struct frame* frame = frame_alloc (spte->uaddr, PAL_USER);
  if (frame == NULL)
    return NULL;

  uint8_t *kpage = frame->kaddr;

  /* Load this page. */
  filesys_lock_acquire (__func__);
  int nread = file_read (spte->file, kpage, spte->read_bytes);
  filesys_lock_release (__func__);

  if (nread != (int) spte->read_bytes)
    {
      frame_free (frame);
      return NULL;
    }
  memset (kpage + spte->read_bytes, 0, spte->zero_bytes);

  /* Add the page to the process's address space. */
  if (!install_page (spte->uaddr, kpage, spte->writable))
    {
      frame_free (frame);
      return NULL;
    }

  return frame;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

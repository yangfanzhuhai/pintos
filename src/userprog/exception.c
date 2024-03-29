#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

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
      thread_exit (); 

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

/* Page fault handler. 

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
//  bool user;         /* True: access by user, false: access by kernel. */
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
//  user = (f->error_code & PF_U) != 0;

  /* If the faulting address is not a present page in the page table, look up
     the supplemental page table for possible data location. */
  if (not_present)
    {
      struct thread *t = thread_current ();

      void *fault_page = pg_round_down (fault_addr);
      
      struct page *supp_page = page_lookup (t->pages, fault_page);      
        
      /* If the supplemental page table contains information for supp_page, 
         load the data according to the page_location_option. 
         */
      if (supp_page != NULL)
        { 
          struct file *file;  
          /* Obtain a frame to store the page. */
          uint8_t *kpage = frame_obtain (PAL_USER, fault_page);
          if (kpage == NULL)
            PANIC ("Fail to get frame for lazy-loading in page_falut_handler.");        
          switch (supp_page->page_location_option)
            {
              /* The data that should be in this page is in the file system. */            
              case FILESYS:
                /* Load this page with the file data recorded in the supp_page. */
                file = supp_page->file;
                file_seek (file, supp_page->ofs);
                if (file_read (file, kpage, supp_page->page_read_bytes) 
                               != (int)supp_page->page_read_bytes)
                  {
                    frame_release (kpage);
                    PANIC ("Fail to read file for lazy-loading in page_falut_handler");
                  }
                memset (kpage + supp_page->page_read_bytes, 0, 
                        PGSIZE - (supp_page->page_read_bytes));
                break;
              
              case SWAPSLOT:
                //printf("Supp: %d\n", supp_page->swap_index);
                swap_from_disk (supp_page->swap_index, kpage);
                break;
  
              case ALLZERO:
                memset (kpage, 0, PGSIZE);
                break;
            }
            
            /* Add the page to the process's address space. */
            if (!install_page (fault_page, kpage, supp_page->writable))
              {
                frame_release (kpage);
                PANIC ("Fail to point the page table entry for the faulting virtual address to the frame. ");
              }             
        }
      else if (is_user_vaddr (fault_addr) && 
                   fault_addr >= f->esp - PUSHA_SIZE 
                      && PHYS_BASE - fault_addr < STACK_LIMIT && write) 
        {
          /* Stack growth */
          /* Obtain a frame for stack growth. */
          uint8_t *kpage = frame_obtain (PAL_USER, fault_page);
          if (kpage == NULL)
            PANIC ("Fail to get frame for lazy-loading in page_falut_handler.");        
          
          /* Add the page to the process's address space. */
          if (!install_page (fault_page, kpage, true))
            {
              frame_release (kpage);
              PANIC ("Fail to point the page table entry for the faulting virtual address to the frame. ");
            }
        }
      else 
        {
          /* If the supplemental page table indicates that the user 
            process should not expect any data at fault_addr, or if 
            the page is not a stack growth case*/
          thread_exit ();
        }
        
    }
  else 
    {
      /* Assess in an attempt to write to a read-only page. */
      thread_exit ();
    }
}


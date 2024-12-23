#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "lib/string.h"

#define MAX_STACK_SIZE (8 * 1024 * 1024)

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);
bool is_stack_access(void *esp, void *fault_addr);
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
void exception_init(void)
{
   /* These exceptions can be raised explicitly by a user program,
      e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
      we set DPL==3, meaning that user programs are allowed to
      invoke them via these instructions. */
   intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
   intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
   intr_register_int(5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

   /* These exceptions have DPL==0, preventing user processes from
      invoking them via the INT instruction.  They can still be
      caused indirectly, e.g. #DE can be caused by dividing by
      0.  */
   intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
   intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
   intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
   intr_register_int(7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
   intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
   intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
   intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
   intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
   intr_register_int(19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

   /* Most exceptions can be handled with interrupts turned on.
      We need to disable interrupts for page faults because the
      fault address is stored in CR2 and needs to be preserved. */
   intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void exception_print_stats(void)
{
   printf("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void kill(struct intr_frame *f)
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
      printf("%s: dying due to interrupt %#04x (%s).\n",
             thread_name(), f->vec_no, intr_name(f->vec_no));
      intr_dump_frame(f);
      thread_exit();

   case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame(f);
      PANIC("Kernel bug - unexpected interrupt in kernel");

   default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name(f->vec_no), f->cs);
      thread_exit();
   }
}

/* Page fault handler.  

   At entry, the address that faulted is in CR2 (Control Register 2) 
   and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  
   The example code here shows how to parse that information.  
   You can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void page_fault(struct intr_frame *f)
{
   bool not_present; /* True: not-present page, false: writing r/o page. */
   bool write;       /* True: access was write, false: access was read. */
   bool user;        /* True: access by user, false: access by kernel. */
   void *fault_addr; /* Fault address. */



   /* Obtain faulting address(virtual address that was accessed to cause the fault)
      - 해당 pointer는 code나 data일 수 있음.
      - 해당 주소에 의해 page fault가 발생한 것이 아닐 수도 있음. (that's f->eip). */
   asm("movl %%cr2, %0" : "=r"(fault_addr));

   /* Turn interrupts back on (they were only off so that we could be assured of reading CR2 before it changed). */
   intr_enable();

   /* Count page faults. */
   page_fault_cnt++;

   /* Determine cause. */
   not_present = (f->error_code & PF_P) == 0;
   write = (f->error_code & PF_W) != 0;
   user = (f->error_code & PF_U) != 0;

   /* Page Fault가 발생하는 3가지 Case 
      1. Process가 요청한 Virtual Memory Page가 물리 메모리에 적재 안된 경우
         [Demanding paging]
            a. Lazy Loading
            b. Swap out
            c. stack overflow
               - static void vm_stack_growth(void *addr)
      2. Write 권한 없는 페이지에 write 시도한 경우 (Writing r/o)
         (!not_present인 경우) : 잘못된 접근
      3. Invalid address에 접근한 경우
         조건 : NULL이거나, is_kernel_vaddr()이거나 spt에도 없는 경우(is_exist_spt(adrr))
   */
   struct thread *cur = thread_current();
   void *esp = user ? f->esp : cur->esp;
   void *upage = pg_round_down(fault_addr);

   if (is_kernel_vaddr(fault_addr) || !not_present)
   {
      if (lock_held_by_current_thread(&frame_lock))
      {
         lock_release(&frame_lock);
      }
      exit(-1);
   }
   lock_acquire(&frame_lock);

   struct spt_entry *entry = spt_find_page(&cur->spt, fault_addr);
   if (entry == NULL)
   {
      if (is_stack_access(esp, fault_addr))
         entry = grow_stack(esp, fault_addr, cur);
      else
      {
         lock_release(&frame_lock);
         exit(-1);
      }     
   }
   void *kpage = frame_allocate(PAL_USER, upage);
   page_load(entry, kpage);
   map_page(entry, upage, kpage, cur);
   if(lock_held_by_current_thread(&frame_lock)) {
      lock_release(&frame_lock);
   }
      
}

bool is_stack_access(void *esp, void *fault_addr)
{
   // 유효한 사용자 주소인지 확인
   if (!is_user_vaddr(fault_addr))
   {
      return false;
   }

   // fault_addr가 스택 영역 내에 있는지 확인
   if (fault_addr < PHYS_BASE - MAX_STACK_SIZE || fault_addr >= PHYS_BASE)
   {
      return false;
   }

   // fault_addr가 스택 확장 조건을 만족하는지 확인
   if (fault_addr >= (uint8_t *)esp - 32)
   {
      return true;
   }
   return false;
}
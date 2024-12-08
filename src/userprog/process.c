#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

#define MAX_STACK_SIZE (8 * 1024 * 1024)

struct lock file_lock;

static thread_func start_process NO_RETURN;
static bool load(const char *cmdline, void (**eip)(void), void **esp);

struct thread *get_child_thread(struct thread *parent, tid_t tid)
{
  struct thread *child;
  struct list_elem *e;

  /* parent의 child_list를 순회 */
  for (e = list_begin(&parent->child_list); e != list_end(&parent->child_list); e = list_next(e))
  {
    child = list_entry(e, struct thread, child_elem);

    if (child->tid == tid)
      return child;
  }

  return NULL;
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t process_execute(const char *file_name)
{
  char pg_name[128];
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME. Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page(0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy(fn_copy, file_name, PGSIZE); // 복사 최대 길이 : PGSIZE - 1 (버퍼 오버플로우 방지)

  /* Make a copy of PROGRAM_NAME */
  size_t length = strcspn(fn_copy, " ");
  strlcpy(pg_name, fn_copy, length + 1);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create(pg_name, PRI_DEFAULT, start_process, fn_copy);

  /* Child list update */
  struct thread *parent = thread_current();
  struct thread *child = get_child_thread(parent, tid);

  if (tid == TID_ERROR || child == NULL)
  {
    palloc_free_page(fn_copy);
    return tid;
  }

  sema_down(&child->wait_sys);

  if (child->exit_flag)
    tid = TID_ERROR;

  return tid;
}

void argument_passing(char **argv, int argc, void **esp)
{
  char *arg_stack_addr[64];

  // Step1. Push each argv[i] value
  for (int i = argc - 1; i >= 0; i--)
  {
    int len = strlen(argv[i]) + 1; // null byte 고려(+1)
    *esp -= len;                   // user stack pointer : len만큼 push
    memcpy(*esp, argv[i], len);
    arg_stack_addr[i] = *esp; // Save : stack pointer address
  }

  // Step2. word-align :  multiple of 4
  while ((uintptr_t)*esp % 4 != 0)
  {
    (*esp)--;
    *(uint8_t *)(*esp) = 0;
  }

  // Step3-1. Push address of argv[argc] : null pointer
  *esp -= sizeof(char *); // Address of "argv[argc] = 0"
  *(uint32_t *)(*esp) = 0;

  // Step3-2. Push each arg_stack_address
  for (int i = argc - 1; i >= 0; i--)
  {
    *esp -= sizeof(char *);
    *(char **)(*esp) = arg_stack_addr[i];
  }

  // Step4. Push argv, argc
  *esp -= 4;
  *(uint32_t *)(*esp) = (uint32_t)(*esp + 4);
  *esp -= 4;
  *(uint32_t *)(*esp) = argc;

  // Step5. Push fake return address
  *esp -= 4;
  *(uint32_t *)(*esp) = 0;
}

/* A thread function that loads a user process and starts it running. */
static void start_process(void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_; // Interrupt Frame
  bool success;          // Program Load 성공 여부

  /* [Project 3] */
  spt_init(&thread_current()->spt);
  mmt_init(&thread_current()->mmt);

  /* Initialize interrupt frame and load executable. */
  memset(&if_, 0, sizeof if_);                            // interrupt Frame 0으로 초기화
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG; // Data segment 설정
  if_.cs = SEL_UCSEG;                                     // Code segment 설정
  if_.eflags = FLAG_IF | FLAG_MBS;                        // Flag 설정 : Interrupt Flag, Must be set

  /*
    Todo: Implementing Argument Paassing (by 80x86 Calling Convention)
      1. 공백을 기준으로 단어 분할
      2. Stack Push : 각 문자열과 Null byte (오른쪽에서 왼쪽으로)
                        +) 4의 배수로 padding 추가
      3. Stack Push : 각 값을 가르키는 stack Address
      4. Stack Push : Fake return Address
  */
  char *argv[64]; // 128bytes 제한 -> 최대 64개 (consider Null byte)
  int argc = 0;

  char *token, *save_ptr;
  for (token = strtok_r(file_name, " ", &save_ptr);
       token != NULL;
       token = strtok_r(NULL, " ", &save_ptr), argc++)
  {
    argv[argc] = token;
  }

  success = load(file_name, &if_.eip, &if_.esp); // User Program 로드

  /* load 성공 여부 저장 및 Parent thread 깨우기 위해 sema_up */
  struct thread *child = thread_current();
  child->exit_flag = !success;
  sema_up(&child->wait_sys);

  /* If load failed, quit. */
  if (!success)
  {
    palloc_free_page(file_name);
    thread_exit();
  }

  /* User Program 실행 전 Stack Argument Setting */
  argument_passing(argv, argc, &if_.esp);
  palloc_free_page(file_name); // copy memory 해제

  /* Stack print */
  // hex_dump((uintptr_t)if_.esp, if_.esp, PHYS_BASE - if_.esp, true);

  /* Start the user process by simulating a return from an interrupt,
    implemented by intr_exit (in threads/intr-stubs.S).
     Because intr_exit takes all of its arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.
   If it was terminated by the kernel (i.e. killed due to an exception), returns -1.
   If TID is invalid or if it was not a child of the calling process,
   or if process_wait() has already been successfully called for the given TID,
   returns -1 immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it does nothing. */
int process_wait(tid_t child_tid UNUSED)
{
  struct thread *parent = thread_current();
  struct thread *child;
  struct list_elem *e;

  // 자식 리스트가 비어 있는 경우 바로 -1 반환
  if (list_empty(&parent->child_list))
    return -1;

  for (e = list_begin(&parent->child_list);
       e != list_end(&parent->child_list); e = list_next(e))
  {
    child = list_entry(e, struct thread, child_elem);

    if (child->tid == child_tid)
    {
      sema_down(&child->wait_sys);          // wait for child to end
      int exit_status = child->exit_status; // save results
      list_remove(&child->child_elem);      // remove element
      sema_up(&child->exit_sys);            // wake a child up

      return exit_status;
    }
  }

  return -1; // "TID is invalid" OR "it was not a child of the Calling Process"
}

/* Free the current process's resources. */
void process_exit(void)
{
  struct thread *cur = thread_current();
  uint32_t *pd;
  

  for (int i = 0; i < cur->mapid; i++)
    munmap(i);

  /* Project3 */
  spt_destroy(&cur->spt);
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
  {
    /* Correct ordering here is crucial.  We must set
       cur->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
    cur->pagedir = NULL;
    pagedir_activate(NULL);
    frame_table_find_entry_delete(cur);
    pagedir_destroy(pd);
  }

  /*oom*/
  for (int i = 2; i < 126; i++)
  {
    if (cur->fd_table[i] != NULL)
      file_close(cur->fd_table[i]);
  }

  palloc_free_page(cur->fd_table);
  file_close(cur->excute_file_name);

  /* sema control for parent, child */
  sema_up(&cur->wait_sys);   // wake a Parent up
  sema_down(&cur->exit_sys); // wait for Parent signal
}

/* Sets up the CPU for running user code in the current thread.
   This function is called on every context switch. */
void process_activate(void)
{
  struct thread *t = thread_current();

  /* Activate thread's page tables. */
  pagedir_activate(t->pagedir);

  /* Set thread's kernel stack for use in processing interrupts. */
  tss_update();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack(void **esp);
static bool validate_segment(const struct Elf32_Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);
static bool install_page(void *upage, void *kpage, bool writable); 

/* For Project 3 */
static void page_zero(void *kpage);
static void page_swap(struct spt_entry *entry, void *kpage);
static void page_file(struct spt_entry *entry, void *kpage);


/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load(const char *file_name, void (**eip)(void), void **esp)
{
  struct thread *t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create();
  printf("dudu <%s>\n", t->name);
  printf("dudu tid <%d>\n", t->tid);
  printf("dudu pdir x <%x>\n", t->pagedir);
  printf("dudu pdir <%d>\n", t->pagedir == NULL);
  
  if (t->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  lock_acquire(&file_lock);
  file = filesys_open(file_name);
  if (file == NULL)
  {
    printf("load: %s: open failed\n", file_name);
    goto done;
  }
  /* load(excutable file open) 성공 시 실행 파일에 쓰기 방지 설정 */
  file_deny_write(file);        // 실행 파일에 대한 쓰기 방지
  t->excute_file_name = file;   // process_exit 때 접근 가능하도록 file 주소 저장

  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 || ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024)
  {
    printf("load: %s: error loading executable\n", file_name);
    goto done;
  }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
  {
    struct Elf32_Phdr phdr;

    if (file_ofs < 0 || file_ofs > file_length(file))
      goto done;
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    file_ofs += sizeof phdr;
    switch (phdr.p_type)
    {
    case PT_NULL:
    case PT_NOTE:
    case PT_PHDR:
    case PT_STACK:
    default:
      /* Ignore this segment. */
      break;
    case PT_DYNAMIC:
    case PT_INTERP:
    case PT_SHLIB:
      goto done;
    case PT_LOAD:
      if (validate_segment(&phdr, file))
      {
        bool writable = (phdr.p_flags & PF_W) != 0;
        uint32_t file_page = phdr.p_offset & ~PGMASK;
        uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
        uint32_t page_offset = phdr.p_vaddr & PGMASK;
        uint32_t read_bytes, zero_bytes;
        if (phdr.p_filesz > 0)
        {
          /* Normal segment.
             Read initial part from disk and zero the rest. */
          read_bytes = page_offset + phdr.p_filesz;
          zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
        }
        else
        {
          /* Entirely zero.
             Don't read anything from disk. */
          read_bytes = 0;
          zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
        }
        if (!load_segment(file, file_page, (void *)mem_page,
                          read_bytes, zero_bytes, writable))
          goto done;
      }
      else
        goto done;
      break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp))
    goto done;

  /* Start address. */
  *eip = (void (*)(void))ehdr.e_entry;

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  lock_release(&file_lock);
  return success;
}

/* Create a minimal stack by mapping a zeroed page at the top of user virtual memory. */
static bool setup_stack(void **esp)
{
  uint8_t *kpage = frame_allocate(PAL_USER | PAL_ZERO, (uint8_t *)PHYS_BASE - PGSIZE);

  if (!kpage)
    return false;
  
  if(!install_page(((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true)){
    frame_deallocate(kpage);
    return false;
  }

  // SPT에 스택 페이지 정보 추가
  struct thread *cur = thread_current();
  if (!spt_add_page(&cur->spt, ((uint8_t *)PHYS_BASE) - PGSIZE, NULL, 0, 0, PGSIZE, true, PAGE_PRESENT)){
    install_page(((uint8_t *)PHYS_BASE) - PGSIZE, NULL, false); // Rollback SPT when fail
    frame_deallocate(kpage);
    return false;
  }

  *esp = PHYS_BASE; 
  return true;
}

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(ofs % PGSIZE == 0);

  file_seek(file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
  {
    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* 
      [Lazy Load Segment 구현]
        이 시점에서 다음과 같은 정보를 저장할 구조체를 새롭게 할당하는 방법
          - file, ofs, page_read_bytes, page_zero_bytes를 저장 (calloc 사용하는 예시를 보는 중)
        이후 추가적인 초기화 함수 동작
        lazy_load_segment 구현해서 함수 포인터를 함께 전달해 초기화
    */

    /* Add SPT entry */
    if (!spt_add_page(&thread_current()->spt, upage, file, ofs, page_read_bytes, page_zero_bytes, writable, PAGE_FILE))
    {
      spt_cleanup_partial(&thread_current()->spt, upage);
      return false;
    }

    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    ofs += page_read_bytes;
    upage += PGSIZE;
  }
  return true;
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
static bool install_page(void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current();

  /* Verify that there's not already a page at that virtual address, then map our page there. */
  return (pagedir_get_page(t->pagedir, upage) == NULL && pagedir_set_page(t->pagedir, upage, kpage, writable));
}

/* For Project 3 - Virtual Memory */
struct spt_entry *grow_stack(void *esp, void *fault_addr, struct thread *cur)
{
  void *upage = pg_round_down(fault_addr);
  spt_add_page(&cur->spt, upage, NULL, 0, 0, PGSIZE, true, PAGE_ZERO);
  return spt_find_page(&cur->spt, upage);
}

void page_load(struct spt_entry *entry, void *kpage)
{
  switch (entry->status)
  {
  case PAGE_ZERO:
    page_zero(kpage);
    break;

  case PAGE_SWAP:
    page_swap(entry, kpage);
    break;

  case PAGE_FILE:
    page_file(entry, kpage);
    break;

  default:
    frame_deallocate(kpage);
    exit(-1);
  }
}

void map_page(struct spt_entry *entry, void *upage, void *kpage, struct thread *cur)
{
  if (!pagedir_set_page(cur->pagedir, upage, kpage, entry->writable))
  {
    frame_deallocate(kpage);
    exit(-1);
  }
  entry->status = PAGE_PRESENT;
}

static void page_zero(void *kpage)
{
  memset(kpage, 0, PGSIZE);
}

static void page_swap(struct spt_entry *entry, void *kpage)
{
  swap_in(entry->swap_index, kpage);
  entry->swap_index = -1;
}

static void page_file(struct spt_entry *entry, void *kpage)
{
  bool was_holding_lock = lock_held_by_current_thread(&file_lock);

  if (!was_holding_lock)
    lock_acquire(&file_lock);
  file_seek(entry->file, entry->ofs);
  if (file_read(entry->file, kpage, entry->page_read_bytes) != (int)entry->page_read_bytes)
  {
    frame_deallocate(kpage);
    if (!was_holding_lock)
      lock_release(&file_lock);
    exit(-1);
  }
  memset(kpage + entry->page_read_bytes, 0, entry->page_zero_bytes);

  if (!was_holding_lock)
    lock_release(&file_lock);
}

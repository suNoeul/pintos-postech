#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <inttypes.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/shutdown.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "devices/rtc.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#else
#include "tests/threads/tests.h"
#endif
#ifdef FILESYS
#include "devices/block.h"
#include "devices/ide.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif
#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/page.h"

/* Page directory with kernel mappings only. */
uint32_t *init_page_dir;

#ifdef FILESYS
/* -f: Format the file system? */
static bool format_filesys;

/* -filesys, -scratch, -swap: Names of block devices to use,
   overriding the defaults. */
static const char *filesys_bdev_name;
static const char *scratch_bdev_name;
#ifdef VM
static const char *swap_bdev_name;
#endif
#endif /* FILESYS */

/* -ul: Maximum number of pages to put into palloc's user pool. */
static size_t user_page_limit = SIZE_MAX;

static void bss_init (void);
static void paging_init (void);

static char **read_command_line (void);
static char **parse_options (char **argv);
static void run_actions (char **argv);
static void usage (void);

#ifdef FILESYS
static void locate_block_devices (void);
static void locate_block_device (enum block_type role, const char *name);
#endif

int main (void) NO_RETURN;

/* Pintos main program. */
int main (void)
{
  char **argv;

  /* Clear BSS. */  
  bss_init ();

  /* Break command line into arguments and parse options. */
  argv = read_command_line ();
  argv = parse_options (argv);

  /* Initialize ourselves as a thread so we can use locks,
     then enable console locking. */
  thread_init ();
  console_init ();  

  /* Greet user. */
  printf ("Pintos booting with %'"PRIu32" kB RAM...\n", init_ram_pages * PGSIZE / 1024);

  /* Initialize memory system. */
  palloc_init (user_page_limit);
  malloc_init ();
  paging_init ();

  /* Segmentation. */
#ifdef USERPROG
  tss_init ();
  gdt_init ();
#endif

  /* Initialize interrupt handlers. */
  intr_init ();
  timer_init ();
  kbd_init ();
  input_init ();
#ifdef USERPROG
  exception_init ();
  syscall_init ();
#endif

  /* init func for Project 3 */
  frame_table_init();
  

  /* Start thread scheduler and enable interrupts. */
  thread_start ();
  serial_init_queue ();
  timer_calibrate ();

#ifdef FILESYS
  /* Initialize file system. */
  ide_init ();  
  locate_block_devices ();
  filesys_init (format_filesys);
#endif
  swap_table_init();

  printf ("Boot complete.\n");
  
  /* Run actions specified on kernel command line. */
  run_actions (argv);

  /* Finish up. */
  shutdown ();
  thread_exit ();
}

/* Clear the "BSS", a segment that should be initialized to
   zeros.  It isn't actually stored on disk or zeroed by the
   kernel loader, so we have to zero it ourselves.

   The start and end of the BSS segment is recorded by the
   linker as _start_bss and _end_bss.  See kernel.lds. */
static void bss_init (void) 
{
  extern char _start_bss, _end_bss;
  memset (&_start_bss, 0, &_end_bss - &_start_bss);
}

/* Populates the base page directory and page table with the
   kernel virtual mapping, and then sets up the CPU to use the
   new page directory.  Points init_page_dir to the page
   directory it creates. */
static void paging_init (void)
{
  uint32_t *pd, *pt;
  size_t page;
  extern char _start, _end_kernel_text;

  pd = init_page_dir = palloc_get_page (PAL_ASSERT | PAL_ZERO);
  pt = NULL;
  for (page = 0; page < init_ram_pages; page++)
    {
      uintptr_t paddr = page * PGSIZE;
      char *vaddr = ptov (paddr);
      size_t pde_idx = pd_no (vaddr);
      size_t pte_idx = pt_no (vaddr);
      bool in_kernel_text = &_start <= vaddr && vaddr < &_end_kernel_text;

      if (pd[pde_idx] == 0)
        {
          pt = palloc_get_page (PAL_ASSERT | PAL_ZERO);
          pd[pde_idx] = pde_create (pt);
        }

      pt[pte_idx] = pte_create_kernel (vaddr, !in_kernel_text);
    }

  /* Store the physical address of the page directory into CR3
     aka PDBR (page directory base register).  This activates our
     new page tables immediately.  See [IA32-v2a] "MOV--Move
     to/from Control Registers" and [IA32-v3a] 3.7.5 "Base Address
     of the Page Directory". */
  asm volatile ("movl %0, %%cr3" : : "r" (vtop (init_page_dir)));
}

/* Breaks the kernel command line into words and returns them as an argv-like array. */
static char ** read_command_line (void) 
{
  static char *argv[LOADER_ARGS_LEN / 2 + 1]; // 최대 많은 인자 들어온다고 가정 ('\0'과 NULL Pointer 공간 고려)
  char *p, *end;
  int argc;
  int i;

  argc = *(uint32_t *) ptov (LOADER_ARG_CNT); 
  p = ptov (LOADER_ARGS);     // p : 명령 인자의 시작 주소
  end = p + LOADER_ARGS_LEN;  // end : 메모리 버퍼 끝 주소
  for (i = 0; i < argc; i++) 
    {
      if (p >= end)
        PANIC ("command line arguments overflow");

      argv[i] = p;
      
      /* strnlen() : p부터 문자열 끝('\0')까지 길이 계산 */
      p += strnlen (p, end - p) + 1; 
    }
  argv[argc] = NULL;

  /* Print kernel command line. */
  printf ("Kernel command line:");
  for (i = 0; i < argc; i++)
    /* strchr() : 문자열에서 특정 문자를 찾는 함수 */
    if (strchr (argv[i], ' ') == NULL)
      printf (" %s", argv[i]);    // 공백이 없으면 그냥 출력
    else
      printf (" '%s'", argv[i]);  // 공백이 있으면 따옴표 감싸서 출력
  printf ("\n");

  return argv;
}

/* Parses options in ARGV[] and returns the first non-option argument. */
static char ** parse_options (char **argv) 
{
  // 모든 옵션은 맨 앞에 배치하도록 구현되어 있음. 
  for (; *argv != NULL && **argv == '-'; argv++) {
      char *save_ptr;
      char *name = strtok_r (*argv, "=", &save_ptr);  // option name 분리
      char *value = strtok_r (NULL, "", &save_ptr);   // option value 분리
      
      if (!strcmp (name, "-h"))       // 도움말
        usage ();
      else if (!strcmp (name, "-q"))  // 종료 
        shutdown_configure (SHUTDOWN_POWER_OFF);
      else if (!strcmp (name, "-r"))  // 재부팅 
        shutdown_configure (SHUTDOWN_REBOOT);
#ifdef FILESYS
      else if (!strcmp (name, "-f"))  // 파일시스템 포멧
        format_filesys = true;
      else if (!strcmp (name, "-filesys"))  // 파일시스템 장치 이름 설정
        filesys_bdev_name = value;
      else if (!strcmp (name, "-scratch"))  // 임시 장치 이름 설정
        scratch_bdev_name = value;
#ifdef VM
      else if (!strcmp (name, "-swap")) 
        swap_bdev_name = value;
#endif
#endif
      else if (!strcmp (name, "-rs"))       // 랜덤 시드 설정
        random_init (atoi (value));
      else if (!strcmp (name, "-mlfqs"))    // MLFQS 스케줄러 활성화 
        thread_mlfqs = true;
#ifdef USERPROG
      else if (!strcmp (name, "-ul"))       // 유저 페이지 제한 설정
        user_page_limit = atoi (value);
#endif
      else
        PANIC ("unknown option `%s' (use -h for help)", name);
  }

  /* Initialize the random number generator based on the system
     time.  This has no effect if an "-rs" option was specified.

     When running under Bochs, this is not enough by itself to
     get a good seed value, because the pintos script sets the
     initial time to a predictable value, not to the local time,
     for reproducibility.  To fix this, give the "-r" option to
     the pintos script to request real-time execution. */
  random_init (rtc_get_time ()); // 랜덤 초기화: "-rs" 옵션이 없으면 시스템 시간 사용
  
  return argv; // 옵션이 아닌 첫 번째 인자 주소 반환
}

/* Runs the task specified in ARGV[1]. */
static void run_task (char **argv)
{
  const char *task = argv[1];
  
  printf ("Executing '%s':\n", task);
#ifdef USERPROG
  process_wait (process_execute (task));
#else
  run_test (task);
#endif
  printf ("Execution of '%s' complete.\n", task);
}

/* Executes all of the actions specified in ARGV[]
   up to the null pointer sentinel. */
static void run_actions (char **argv) 
{
  /* An action. */
  struct action 
    {
      char *name;                       /* Action name. */
      int argc;                         /* # of args, including action name. */
      void (*function) (char **argv);   /* Function to execute action. */
    };

  /* Table of supported actions. */
  static const struct action actions[] = 
    {
      {"run", 2, run_task},             // 유저 프로그램 실행
#ifdef FILESYS
      {"ls", 1, fsutil_ls},             // 파일 리스트 출력
      {"cat", 2, fsutil_cat},           // 파일 내용 출력
      {"rm", 2, fsutil_rm},             // 파일 삭제
      {"extract", 1, fsutil_extract},   // 아카이브 추출
      {"append", 2, fsutil_append},     // 파일에 내용 추가
#endif
      {NULL, 0, NULL},                  // 더 이상 유효한 명령어가 없음을 알리는 종료 조건
    };

  while (*argv != NULL) {
    const struct action *a;
    int i;

    /* Find action name. */
    for (a = actions; ; a++)
      if (a->name == NULL)
        PANIC ("unknown action `%s' (use -h for help)", *argv);
      else if (!strcmp (*argv, a->name))
        break;

    /* Check for required arguments. */
    for (i = 1; i < a->argc; i++)
      if (argv[i] == NULL)
        PANIC ("action `%s' requires %d argument(s)", *argv, a->argc - 1);

    /* Invoke action and advance. */
    a->function (argv);
    argv += a->argc;
  }
  
}

/* Prints a kernel command line help message and powers off the
   machine. */
static void usage (void)
{
  printf ("\nCommand line syntax: [OPTION...] [ACTION...]\n"
          "Options must precede actions.\n"
          "Actions are executed in the order specified.\n"
          "\nAvailable actions:\n"
#ifdef USERPROG
          "  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
#else
          "  run TEST           Run TEST.\n"
#endif
#ifdef FILESYS
          "  ls                 List files in the root directory.\n"
          "  cat FILE           Print FILE to the console.\n"
          "  rm FILE            Delete FILE.\n"
          "Use these actions indirectly via `pintos' -g and -p options:\n"
          "  extract            Untar from scratch device into file system.\n"
          "  append FILE        Append FILE to tar file on scratch device.\n"
#endif
          "\nOptions:\n"
          "  -h                 Print this help message and power off.\n"
          "  -q                 Power off VM after actions or on panic.\n"
          "  -r                 Reboot after actions.\n"
#ifdef FILESYS
          "  -f                 Format file system device during startup.\n"
          "  -filesys=BDEV      Use BDEV for file system instead of default.\n"
          "  -scratch=BDEV      Use BDEV for scratch instead of default.\n"
#ifdef VM
          "  -swap=BDEV         Use BDEV for swap instead of default.\n"
#endif
#endif
          "  -rs=SEED           Set random number seed to SEED.\n"
          "  -mlfqs             Use multi-level feedback queue scheduler.\n"
#ifdef USERPROG
          "  -ul=COUNT          Limit user memory to COUNT pages.\n"
#endif
          );
  shutdown_power_off ();
}

#ifdef FILESYS
/* Figure out what block devices to cast in the various Pintos roles. */
static void locate_block_devices (void)
{
  locate_block_device (BLOCK_FILESYS, filesys_bdev_name);
  locate_block_device (BLOCK_SCRATCH, scratch_bdev_name);
#ifdef VM
  locate_block_device (BLOCK_SWAP, swap_bdev_name);
#endif
}

/* Figures out what block device to use for the given ROLE: the
   block device with the given NAME, if NAME is non-null,
   otherwise the first block device in probe order of type
   ROLE. */
static void locate_block_device (enum block_type role, const char *name)
{
  struct block *block = NULL;

  if (name != NULL)
    {
      block = block_get_by_name (name);
      if (block == NULL)
        PANIC ("No such block device \"%s\"", name);
    }
  else
    {
      for (block = block_first (); block != NULL; block = block_next (block))
        if (block_type (block) == role)
          break;
    }

  if (block != NULL)
    {
      printf ("%s: using %s\n", block_type_name (role), block_name (block));
      block_set_role (role, block);
    }
}
#endif

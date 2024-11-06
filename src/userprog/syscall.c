#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

#include "devices/shutdown.h"
#include "userprog/process.h"

/* Process identifier. */
typedef int pid_t;

static void syscall_handler (struct intr_frame *);
void check_address();

void syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
 
static void syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_number = f->eax;

  switch(syscall_number){
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      exit(f->edi);
      break;
    case SYS_EXEC:
      f->eax = exec(f->edi);
      break;
    case SYS_WAIT:
      f->eax = wait(f->edi);
      break;
    case SYS_CREATE:
      break;
    case SYS_REMOVE:
      break;
    case SYS_OPEN:
      break;
    case SYS_FILESIZE:
      break;
    case SYS_READ:
      break;
    case SYS_WRITE:
      break;
    case SYS_SEEK:
      break;
    case SYS_TELL:
      break;
    case SYS_CLOSE:
      break;
    default:
      // exit(-1);
      printf ("system call!\n");
      thread_exit ();
  }
}

void check_address(const void *addr)
{
  if (addr == NULL || 
      !is_user_vaddr(addr) || 
      pagedir_get_page(thread_current()->pagedir, addr) == NULL)
    exit(-1);
}

void halt(void) 
{
  shutdown_power_off();
}

void exit(int status) 
{
  struct thread *cur = thread_current();

  /* Check status [status == 0 is success state] */
  printf("%s: exit %d \n", cur->name, status);
  cur->exit_status = status;
  // fd 돌면서 NULL 아닌 것들 close 해주기
  thread_exit();
}

pid_t exec(const char *cmd_line) 
{
  // Runs the excutable : "cmd_line"
  // return : new process's pid (실패 시 -1 반환)
  // Todo: load 실패시 thread_exit() call 됨. 해당 사항 고려할 것
  return process_execute(cmd_line);
}

int wait(pid_t pid) 
{
  // 자식 프로세스 pid를 기다림 && child's exit status 찾기
  // terminate될 때까지 대기 -> pid가 넘긴 exit 코드 반환
  // if (kernel에 의해 종료 : kill()) -1 반환 
  /* 
    다음 조건에 대해 반드시 실패(-1 return)해야 함
    1. pid가 직접적인 자식이 아닌 경우
      - exec의 return 값으로 pid를 받음 (이를 저장해둬야 함)
    2. wait하고 대기 중일 때, 또 wait이 호출된 경우
  */
 return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
  // 파일 생성 : filesys_create() 함수 사용
}
 
bool remove(const char *file)
{
  // 파일 제거 : filesys_remove()
}
 
int open(const char *file)
{

}
 
int filesize(int fd)
{

}
 
int read(int fd, void *buffer, unsigned size)
{

}
 
int write(int fd, const void *buffer, unsigned size)
{
  // if(fd == STDOUT_FILENO)
  //   putbuf(buffer, length);
  // return length;
}

void seek(int fd, unsigned position)
{

}
 
unsigned tell(int fd)
{

}
 
void close(int fd)
{

}
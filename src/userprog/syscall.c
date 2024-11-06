#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"


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
      printf("halt!\n");
      halt();
      break;
    case SYS_EXIT:
      printf("exit!\n");
      exit(f->edi);
      break;
    case SYS_EXEC:
      printf("exec!\n");
      exec(f->edi);
      break;
    case SYS_WAIT:
      printf("wait!\n");
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
  struct thread *cur = thread_current();
  if (addr == NULL || !is_user_vaddr(addr) || pagedir_get_page(cur->pagedir, addr) == NULL)
  {
    /*임의의 잘못된 주소를 참조해서 페이지 폴트 발생시키기. 그냥 exit하는 방법으로도 구현가능*/
    int *invalid_access = (int *)0xFFFFFFFF;
    int value = *invalid_access;
    // exit(-1);
  }
}


void halt(void) 
{
  // pintos 종료 함수
}

void exit(int status) 
{
  // 현재 프로세스 종료 = 현재 스레드 종료
  // thread_exit()
}

pid_t exec(const char cmd_line) 
{
  // 
}

int wait(pid_t pid) 
{

}
 
/*
bool create(const char file, unsigned initial_size)
{
  // 파일 생성 : filesys_create() 함수 사용
}
 
bool remove(const char file)
{
  // 파일 제거 : filesys_remove()
}
 
int open(const char file)
{

}
 
int filesize(int fd)
{

}
 
int read(int fd, void buffer, unsigned size)
{

}
 
int write(int fd, const void buffer, unsigned size)
{

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

*/
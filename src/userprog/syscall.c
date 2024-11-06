#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.c"

static void syscall_handler(struct intr_frame *);
void check_address(const void *addr);
int alloc_fdt(struct file *f);

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f UNUSED)
{
  printf("system call!\n");
  thread_exit();

  // syscall Number에 따라 switch 구문
  // Stack Pointer, Address argument : is_user_vaddr?
  // 유저 스택 인자들 커널 저장
  // %eax = return value
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

bool create(const char *file, unsigned initial_size)
{
  // 파일 생성 : filesys_create() 함수 사용
  check_address(file);
  return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
  // 파일 제거 : filesys_remove()
  check_address(file);
  return filesys_remove(file);
}

int open(const char *file)
{
  int fd_idx;
  struct file *f;
  check_address(file);
  f = filesys_open(file);
  if (f != NULL)
  {
    fd_idx = alloc_fdt(f);
    if (fd_idx != -1)
      return fd_idx;
    else
    {
      file_close(f);
      return -1;
    }
  }
  return -1;
}

int filesize(int fd)
{
  if (fd > 1 && fd < MAX_FD)
    return thread_current()->fd_table[fd]->inode->data.length;
  return -1;
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

void check_address(const void *addr)
{
  if (addr == NULL || !is_user_vaddr(addr) || pagedir_get_page(thread_current()->pagedir, addr) == NULL)
  {
    /*임의의 잘못된 주소를 참조해서 페이지 폴트 발생시키기. 그냥 exit하는 방법으로도 구현가능*/
    int *invalid_access = (int *)0xFFFFFFFF;
    int value = *invalid_access;
  }
}

int alloc_fdt(struct file *f)
{
  struct thread *cur = thread_current();
  int fd_idx;
  // 빈 슬롯 찾기
  for (fd_idx = 2; fd_idx < MAX_FD; fd_idx++)
  { // 0과 1은 보통 표준 입출력용으로 예약
    if (cur->fd_table[fd_idx] == NULL)
    {
      cur->fd_table[fd_idx] = f;
      return fd_idx;
    }
  }
  return -1;
}
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.c"
#include "threads/synch.h"

#define MAX_FD 128 /*128로 정한 이유는 없음.*/

static void syscall_handler(struct intr_frame *);
void check_address(const void *addr);
int alloc_fdt(struct file *f);

void rw_lock_acquire_read(struct rw_lock lock);
void rw_lock_release_read(struct rw_lock lock);
void rw_lock_acquire_write(struct rw_lock lock);
void rw_lock_release_write(struct rw_lock lock);
void rw_lock_init(void);

struct rw_lock filesys_lock;

void syscall_init(void)
{
  rw_lock_init();
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
  // 임시
  struct thread *t = thread_current();
  // 임시

  // 현재 프로세스 종료 = 현재 스레드 종료

  /*palloc했던 fd_table 할당해제, t는 현재 실행중인 thread라고 가정했음.*/
  if (t->fd_table != NULL)
  {
    palloc_free_page(t->fd_table);
    t->fd_table = NULL;
  }

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
    return file_length(thread_current()->fd_table[fd]);
  return -1;
}

int read(int fd, void *buffer, unsigned size)
{
  int i;
  if (fd == 0)
  {
    /*표준 입력*/
    char *char_buffer = (char *)buffer;
    for (i = 0; i < size; i++)
    {
      char c = input_getc();
      char_buffer[i] = c;
    }
    return size;
  }
  else if (fd == 1)
  {
    /*표준 출력*/
    return -1;
  }
  else if (fd > 1 && fd < MAX_FD)
  {
    struct file *f = thread_current()->fd_table[fd];
    rw_lock_acquire_read(filesys_lock);
    file_read(f, buffer, size);
    rw_lock_release_read(filesys_lock);
    return size;
  }
  return -1;
}

int write(int fd, const void *buffer, unsigned size)
{
  if (fd == 0)
  {
    /*표준 입력*/
    return -1;
  }
  else if (fd == 1)
  {
    /*표준 출력*/
    putbuf((const char *)buffer, size);
    return size;
  }
  else if (fd > 1 && fd < MAX_FD)
  {
    struct file *f = thread_current()->fd_table[fd];
    rw_lock_acquire_write(filesys_lock);
    file_write(f, buffer, size);
    rw_lock_release_write(filesys_lock);
    return size;
  }
  return -1;
}

void seek(int fd, unsigned position)
{
  if (fd > 1 && fd < MAX_FD)
  {
    struct file *f = thread_current()->fd_table[fd];
    file_seek(f, position);
  }
}

unsigned tell(int fd)
{
  if (fd > 1 && fd < MAX_FD)
  {
    struct file *f = thread_current()->fd_table[fd];
    return file_tell(f)
  }
  return -1;
}

void close(int fd)
{
  if (fd > 1 && fd < MAX_FD)
  {
    thread_current()->fd_table[fd] = NULL;
  }
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

void filesys_lock_init(void)
{
  lock_init(&filesys_lock.mutex);
  cond_init(&filesys_lock.readers_ok);
  cond_init(&filesys_lock.writer_ok);
  filesys_lock.readers = 0;
  filesys_lock.writer = false;
}

void rw_lock_acquire_read(struct rw_lock lock)
{
  lock_acquire(&lock.mutex);
  while (lock.writer)
    cond_wait(lock.readers_ok, lock.mutex);
  lock.readers++;
  lock_release(&lock.mutex);
}

void rw_lock_release_read(struct rw_lock lock)
{
  lock_acquire(&lock.mutex);
  lock.readers--;
  if (lock.readers == 0)
    cond_signal(&lock.writer_ok, &lock.mutex);
  lock_release(&lock.mutex);
}

void rw_lock_acquire_write(struct rw_lock lock)
{
  lock_acquire(&lock.mutex);
  while (lock.writer || lock.readers > 0)
    cond_wait(&lock.writer_ok, &lock.mutex);
  lock.writer = true;
  lock_release(&lock.mutex);
}

void rw_lock_release_write(struct rw_lock lock)
{
  lock_acquire(&lock.mutex);
  lock.writer = false;
  cond_broadcast(&lock.readers_ok, &lock.mutex);
  cond_signal(&lock.writer_ok, &lock.mutex);
  lock_release(&lock.mutex);
}
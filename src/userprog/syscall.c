#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"


#define MAX_FD 128 /*128로 정한 이유는 없음.*/

/* Process identifier. */
struct rw_lock filesys_lock;

static void syscall_handler(struct intr_frame *f);

/* Handler functions according to syscall_number */
void halt(void);
void exit(int status);
tid_t exec(const char *cmd_line);
int wait(tid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

/* Additional user-defined functions */
void check_address(const void *addr);
int alloc_fdt(struct file *f);

void filesys_lock_init(void);
void rw_lock_acquire_read(struct rw_lock lock);
void rw_lock_release_read(struct rw_lock lock);
void rw_lock_acquire_write(struct rw_lock lock);
void rw_lock_release_write(struct rw_lock lock);

void syscall_init(void)
{
  filesys_lock_init();
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f)
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
      // create();
      break;
    case SYS_REMOVE:
      // remove();
      break;
    case SYS_OPEN:
      // open();
      break;
    case SYS_FILESIZE:
      // filesize();
      break;
    case SYS_READ:
      // read();
      break;
    case SYS_WRITE:
      // write();
      break;
    case SYS_SEEK:
      // seek();
      break;
    case SYS_TELL:
      tell(f->edi);
      break;
    case SYS_CLOSE:
      close(f->edi);
      break;
    default:
      // exit(-1);
      printf ("system call!\n");
      thread_exit ();
  }
}

/* Handler functions according to syscall_number */
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

tid_t exec(const char *cmd_line) 
{
  // Runs the excutable : "cmd_line"
  // return : new process's pid (실패 시 -1 반환)
  // Todo: load 실패시 thread_exit() call 됨. 해당 사항 고려할 것
  return process_execute(cmd_line);
}

int wait(tid_t pid)
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
    return file_tell(f);
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

/* Additional user-defined functions */
void check_address(const void *addr)
{
  if (addr == NULL || 
      !is_user_vaddr(addr) || 
      pagedir_get_page(thread_current()->pagedir, addr) == NULL)
    exit(-1);
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
    cond_wait(&lock.readers_ok, &lock.mutex);
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
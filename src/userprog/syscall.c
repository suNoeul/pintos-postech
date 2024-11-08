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
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"

#define MAX_FD 126 /*1022로 정한 이유는 없음.*/

/* Process identifier. */
struct rw_lock filesys_lock;

static void syscall_handler(struct intr_frame *f);
/* Additional user-defined functions */
void check_address(const void *addr);
int alloc_fdt(struct file *file_);

void filesys_lock_init(void);
void rw_lock_acquire_read(struct rw_lock *lock);
void rw_lock_release_read(struct rw_lock *lock);
void rw_lock_acquire_write(struct rw_lock *lock);
void rw_lock_release_write(struct rw_lock *lock);

void syscall_init(void)
{
  filesys_lock_init();
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f)
{
  int syscall_number = *(int *)f->esp;
  switch (syscall_number)
  {
  case SYS_HALT:
    halt();
    break;
  case SYS_EXIT:
    exit(*(int *)(f->esp + 4));
    break;
  case SYS_EXEC:
    f->eax = exec(*(const char **)(f->esp + 4));
    break;
  case SYS_WAIT:
    f->eax = wait(*(pid_t *)(f->esp + 4));
    break;
  case SYS_CREATE:
    f->eax = create(*(const char **)(f->esp + 16), *(unsigned *)(f->esp + 20));
    break;
  case SYS_REMOVE:
    f->eax = remove(*(const char **)(f->esp + 4));
    break;
  case SYS_OPEN:
    f->eax = open(*(const char **)(f->esp + 4));
    break;
  case SYS_FILESIZE:
    f->eax = filesize(*(int *)(f->esp + 4));
    break;
  case SYS_READ:
    f->eax = read(*(int *)(f->esp + 20),
                  *(const void **)(f->esp + 24),
                  *(unsigned *)(f->esp + 28));
    break;
  case SYS_WRITE:
    f->eax = write(*(int *)(f->esp + 20),
                   *(const void **)(f->esp + 24),
                   *(unsigned *)(f->esp + 28));
    break;
  case SYS_SEEK:
    seek(*(int *)(f->esp + 16), *(unsigned *)(f->esp + 20));
    break;
  case SYS_TELL:
    f->eax = tell(*(int *)(f->esp + 4));
    break;
  case SYS_CLOSE:
    close(*(int *)(f->esp + 4));
    break;
  default:
    printf("Not Defined system call!\n");
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

  printf("%s: exit(%d)\n", cur->name, status);
  cur->exit_status = status;
  for (int i = 2; i < MAX_FD; i++)
  {
    if (cur->fd_table[i] != NULL)
      close(i);
  }
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
  check_address(file);

  struct file *f = filesys_open(file);

  if (f == NULL)
  {
    return -1;
  }
  else
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
}

int filesize(int fd)
{

  if (fd > 1 && fd < MAX_FD)
    return file_length(thread_current()->fd_table[fd]);
  return -1;
}

int read(int fd, void *buffer, unsigned size)
{
  check_address(buffer);
  unsigned int i;
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
    rw_lock_acquire_read(&filesys_lock);
    file_read(f, buffer, size);
    rw_lock_release_read(&filesys_lock);
    return size;
  }
  return -1;
}

int write(int fd, const void *buffer, unsigned size)
{
  check_address(buffer);
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
    if (!f)
    {
      return -1;
    }
    file_write(f, buffer, size);
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
  struct thread *cur = thread_current();
  struct file *file_ = cur->fd_table[fd];
  if (file_ == NULL)
    exit(-1);
  else
  {
    file_ = NULL;
    // file_close(file_);
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

int alloc_fdt(struct file *file_)
{
  struct thread *cur = thread_current();
  int fd_idx;
  for (int i = 2; i < MAX_FD; i++)
  {
    if (cur->fd_table[i] == file_)
    {
      file_deny_write(file_); // 중복된 파일에 쓰기 금지 설정
      break;
    }
  }
  /* Find idx of empty slot : 0과 1은 보통 표준 입출력용으로 예약 */
  for (fd_idx = 2; fd_idx < MAX_FD; fd_idx++)
  {
    if (cur->fd_table[fd_idx] == NULL)
    {
      // if strcmp(cur->name, f) is equal, file_deny_write(f); 추가해야 하나?
      cur->fd_table[fd_idx] = file_;
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

void rw_lock_acquire_read(struct rw_lock *lock)
{
  lock_acquire(&lock->mutex);
  while (lock->writer)
    cond_wait(&lock->readers_ok, &lock->mutex);
  lock->readers++;
  lock_release(&lock->mutex);
}

void rw_lock_release_read(struct rw_lock *lock)
{
  lock_acquire(&lock->mutex);
  lock->readers--;
  if (lock->readers == 0)
    cond_signal(&lock->writer_ok, &lock->mutex);
  lock_release(&lock->mutex);
}

void rw_lock_acquire_write(struct rw_lock *lock)
{
  lock_acquire(&lock->mutex);
  while (lock->writer || lock->readers > 0)
    cond_wait(&lock->writer_ok, &lock->mutex);
  lock->writer = true;
  lock_release(&lock->mutex);
}

void rw_lock_release_write(struct rw_lock *lock)
{
  lock_acquire(&lock->mutex);
  lock->writer = false;
  cond_broadcast(&lock->readers_ok, &lock->mutex);
  cond_signal(&lock->writer_ok, &lock->mutex);
  lock_release(&lock->mutex);
}
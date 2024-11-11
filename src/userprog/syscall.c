#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
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

#define MAX_FD 128  /* limit of 128 openfiles per process [Pintos Manual]*/

/* Process identifier. */
struct lock file_lock;

static void syscall_handler(struct intr_frame *f);

/* Additional user-defined functions */
void check_address(const void *addr);
int find_idx_of_empty_slot(struct file *file);
struct file *get_file_from_fd(int fd);


void syscall_init(void)
{
  lock_init(&file_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f)
{
  /* User stack에서 참조하는 값은 안전한지 확인 */
  check_address(f->esp);
  int syscall_number = *(int *)f->esp;
  
  /* NOTE : Pointer를 참조하는 인자에 대해서 check_address */
  switch (syscall_number) {
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      check_address(f->esp + 4);
      exit(*(int *)(f->esp + 4));
      break;
    case SYS_EXEC:
      f->eax = exec(*(const char **)(f->esp + 4));
      break;
    case SYS_WAIT:
      check_address(f->esp + 4);
      f->eax = wait(*(pid_t *)(f->esp + 4));
      break;
    case SYS_CREATE:
      check_address(f->esp+20);
      f->eax = create(*(const char **)(f->esp + 16), *(unsigned *)(f->esp + 20));
      break;
    case SYS_REMOVE:
      f->eax = remove(*(const char **)(f->esp + 4));
      break;
    case SYS_OPEN:
      f->eax = open(*(const char **)(f->esp + 4));
      break;
    case SYS_FILESIZE:
      check_address(f->esp + 4);
      f->eax = filesize(*(int *)(f->esp + 4));
      break;
    case SYS_READ:
      check_address(f->esp + 20);
      check_address(f->esp + 28);
      f->eax = read(*(int *)(f->esp + 20),
                    *(void **)(f->esp + 24),
                    *(unsigned *)(f->esp + 28));
      break;
    case SYS_WRITE:
      check_address(f->esp + 20);
      check_address(f->esp + 28);
      f->eax = write(*(int *)(f->esp + 20),
                     *(const void **)(f->esp + 24),
                     *(unsigned *)(f->esp + 28));
      break;
    case SYS_SEEK:
      check_address(f->esp + 16);
      check_address(f->esp + 20);
      seek(*(int *)(f->esp + 16), *(unsigned *)(f->esp + 20));
      break;
    case SYS_TELL:
      check_address(f->esp);
      f->eax = tell(*(int *)(f->esp + 4));
      break;
    case SYS_CLOSE:
      check_address(f->esp);
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
  check_address(file);
  bool result;

  lock_acquire(&file_lock);
  result = filesys_create(file, initial_size);
  lock_release(&file_lock);
  
  return result;
}

bool remove(const char *file)
{
  check_address(file);
  bool result;

  // Check : 실행 중인 파일에 대해 remove하는 경우, Unix 표준 처리 방식으로 구현해야 함.
  lock_acquire(&file_lock);
  result = filesys_remove(file);
  lock_release(&file_lock);

  return result;
}

int open(const char *file)
{
  check_address(file);

  lock_acquire(&file_lock);
  struct file *f = filesys_open(file);
  lock_release(&file_lock);

  /* NULL Check */
  if (f == NULL)
    return -1;

  /* Return "fd(file_discriptor)" or "-1" */
  int fd_idx = find_idx_of_empty_slot(f);
  if (fd_idx != -1){
    /* file 이름이 Current Thread와 이름이 같은 경우 : 실행파일이 수정되면 안됨. */
    if(strcmp(thread_current()->name, file) == 0)
      file_deny_write(f);
    return fd_idx;
  }
  else {
    lock_acquire(&file_lock);
    file_close(f);
    lock_release(&file_lock);
    return -1;
  }
}

int filesize(int fd)
{
  struct file* file = get_file_from_fd(fd);
  if(file != NULL)
    return file_length(file);
  else
    return -1;
}

int read(int fd, void *buffer, unsigned size)
{
  check_address(buffer);
  unsigned int count;

  /* 1(STDOUT_FILENO) */
  if (fd == 1) 
    return -1;
  
  /* 0(STDIN_FILENO) */
  if (fd == 0) { 
    char *char_buffer = (char *)buffer;
    for (count = 0; count < size; count++) {
      char c = input_getc();
      char_buffer[count] = c;
      if(c == '\0') break;
    }  
    return count;
  }
  else { /* Else part */      
    struct file *file = get_file_from_fd(fd);
    if(file != NULL){
      lock_acquire(&file_lock);
      count = file_read(file, buffer, size);
      lock_release(&file_lock);
      return count;
    }
  } 
  return -1;
}

int write(int fd, const void *buffer, unsigned size)
{
  check_address(buffer);
  unsigned int count;

  /* 0(STDIN_FILENO) */
  if (fd == 0)
    return -1;

  /* 1(STDOUT_FILENO) */ 
  if (fd == 1) {
    putbuf((const char *)buffer, size);
    return size;
  }
  else {
    struct file *file = get_file_from_fd(fd);
    if(file != NULL){
      // if (get_deny_write(f))
      //   file_deny_write(f);
      lock_acquire(&file_lock);
      count = file_write(file, buffer, size);
      lock_release(&file_lock);
      return count;
    }
  }
  return -1;
}

void seek(int fd, unsigned position)
{
  struct file *file = get_file_from_fd(fd);
  if(file != NULL)
    file_seek(file, position);
}

unsigned tell(int fd)
{
  struct file *file = get_file_from_fd(fd);
  if(file != NULL)
    return file_tell(file);
  else
    return -1;
}

void close(int fd)
{
  struct thread *cur = thread_current();
  struct file *file = get_file_from_fd(fd);
  if(file == NULL)
    exit(-1);
  else {
    cur->fd_table[fd] = NULL;
    file_close(file);
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

int find_idx_of_empty_slot(struct file *file)
{
  struct thread *cur = thread_current();
  int fd_idx;

  /* Find idx of empty slot : 0(STDIN_FILENO), 1(STDOUT_FILENO) */
  for (fd_idx = 2; fd_idx < MAX_FD; fd_idx++) {
    if (cur->fd_table[fd_idx] == NULL) {
      cur->fd_table[fd_idx] = file;
      return fd_idx;
    }
  }
  
  return -1;
}

struct file *get_file_from_fd(int fd)
{
  /* Step1. fd valid check */
  if (!(fd > 1 && fd < MAX_FD))
    return NULL;

  /* Step2. return file address */
  return thread_current()->fd_table[fd];
}
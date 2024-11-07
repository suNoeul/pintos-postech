#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdio.h>
#include "threads/synch.h"

typedef int tid_t;

struct rw_lock
{
    struct lock mutex;
    struct condition readers_ok;
    struct condition writer_ok;
    int readers;
    bool writer;
};

void syscall_init(void);

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

#endif /* userprog/syscall.h */

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdio.h>
#include "threads/synch.h"
#include "lib/user/syscall.h"

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
pid_t exec(const char *cmd_line);
int wait(pid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

#endif /* userprog/syscall.h */
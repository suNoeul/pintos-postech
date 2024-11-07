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

#endif /* userprog/syscall.h */
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

struct rw_lock
{
    struct lock mutex;
    struct condition readers_ok;
    struct condition writer_ok;
    int readers;
    bool writer;
};

#endif /* userprog/syscall.h */

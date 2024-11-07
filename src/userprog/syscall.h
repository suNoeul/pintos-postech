#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdio.h>

struct rw_lock
{
    struct lock mutex;
    struct condition readers_ok;
    struct condition writer_ok;
    int readers;
    bool writer;
};

void syscall_init(void);

/* Additional user-defined functions */
void check_address(const void *addr);
int alloc_fdt(struct file *f);

void filesys_lock_init(void);
void rw_lock_acquire_read(struct rw_lock lock);
void rw_lock_release_read(struct rw_lock lock);
void rw_lock_acquire_write(struct rw_lock lock);
void rw_lock_release_write(struct rw_lock lock);

#endif /* userprog/syscall.h */

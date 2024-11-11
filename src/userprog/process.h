#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct thread *get_child_thread(struct thread *parent, tid_t tid);  // Add user define function
tid_t process_execute (const char *file_name);
void  argument_passing(char **argv, int argc, void **esp);          // Add new passing function
int   process_wait (tid_t);
void  process_exit (void);
void  process_activate (void);

#endif /* userprog/process.h */

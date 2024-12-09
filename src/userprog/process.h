#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "vm/frame.h"
#include "vm/page.h"



struct thread *get_child_thread(struct thread *parent, tid_t tid);  // Add user define function (project2)
tid_t process_execute (const char *file_name);
void  argument_passing(char **argv, int argc, void **esp);          // Add new passing function (project2)
int   process_wait (tid_t);
void  process_exit (void);
void  process_activate (void);

/* For project 3 */
struct spt_entry *grow_stack(void *esp, void *fault_addr, struct thread *cur);
void page_load(struct spt_entry *entry, void *kpage);
void map_page(struct spt_entry *entry, void *upage, void *kpage, struct thread *cur);

#endif /* userprog/process.h */

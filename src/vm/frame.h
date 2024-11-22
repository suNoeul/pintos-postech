#ifndef FRAME_H
#define FRAME_H

#include "threads/thread.h"

struct frame_entry {
    void *frame;             /* Start address of Frame */
    tid_t tid;               /* Frame 소유한 thread ID */
    bool is_allocated;       /* 할당 여부              */
    struct list_elem elem;   /* 리스트 관리 요소       */

    uint32_t *pte;  // 필요한가?
    void *vaddr;    // 필요한가?
};

void frame_init(void);

void * frame_alloc (enum palloc_flags flags) ;
void frame_free(void *frame);
void set_frame_entry(void* frame);
void *evict_frame(void);

#endif /* FRAME_H */

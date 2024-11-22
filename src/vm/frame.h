#ifndef FRAME_H
#define FRAME_H

#include "threads/thread.h"
#include "threads/palloc.h"

struct frame_table_entry
{
    void *frame;           // 물리 프레임 주소
    void *upage;           // 가상 메모리 주소 (User Page)
    struct thread *owner;  // 이 프레임을 소유한 스레드
    bool pinned;           // 핀 여부 (페이지 교체 방지)
    struct list_elem elem; // Frame Table 리스트 요소
};

void frame_init(void);
void *frame_alloc(enum palloc_flags flags, void *upage);

void frame_free(void *frame);
void set_frame_entry(void* frame);
void *evict_frame(void);

#endif /* FRAME_H */

#ifndef FRAME_H
#define FRAME_H

#include "threads/thread.h"
#include "threads/palloc.h"
#include "vm/page.h"

struct frame_table_entry
{
    void *frame;            // 물리 프레임 주소
    void *upage;            // 가상 메모리 주소 (User Page)
    struct thread *owner;   // 이 프레임을 소유한 스레드
    bool pinned;            // 핀 여부 (페이지 교체 방지)
    
    struct list_elem elem;  // Frame Table 리스트 요소
};



void frame_table_init(void);
void *frame_allocate(enum palloc_flags flags, void *upage);
void frame_deallocate(void *frame);
bool remove_frame_entry(void *frame);
bool frame_evict(void);
struct frame_table_entry *frame_find_entry(void *frame);

#endif /* FRAME_H */

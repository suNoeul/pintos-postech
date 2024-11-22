#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>

struct list frame_table;       /* 프레임 테이블 */

struct lock frame_table_lock;  /* 동기화 락 */


/* functionality to manage frame_table */
static bool add_frame_entry (void *frame, void *upage);
static void remove_frame_entry (void *frame);
static struct frame_entry *get_frame_entry (void *frame);
static struct frame_entry *find_evict_frame (void);
static bool save_evicted_frame (struct frame_entry *);


void frame_init(void){
    list_init(&frame_table);          /* 프레임 테이블 초기화 */
    lock_init(&frame_table_lock);     /* 락 초기화 */
}

void *frame_alloc(enum palloc_flags flags, void *upage)
{
    lock_acquire(&frame_table_lock);
    void *frame = NULL;

    frame = palloc_get_page(flags);

    /* Error handling */
    if(frame == NULL){
        // 실패 시, evict frame을 통해 기존 프레임을 교체한 후 프레임 확보
    }

    add_frame_entry(frame, upage);
    lock_release(&frame_table_lock);
    return frame;
}

void frame_free(void *frame){
    // remove_frame(frame);
    // palloc_free_page(frame);

    lock_acquire(&frame_table_lock);

    struct list_elem *e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct frame_table_entry *fte = list_entry(e, struct frame_table_entry, elem);
        if (fte->frame == frame)
        {
            list_remove(e);          // Frame Table에서 제거
            palloc_free_page(frame); // 물리 메모리 반환
            free(fte);               // Frame Table Entry 해제
            break;
        }
    }

    lock_release(&frame_table_lock);
}

void set_frame(void* frame){

}

/* Frame 교체 함수 */
void *evict_frame(void){
    struct frame_entry *fe;

    fe = find_evict_frame();
}

static bool add_frame_entry(void *frame, void *upage)
{
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    if (fte == NULL) {
        lock_release(&frame_table_lock);
        return NULL;
    }
    fte->frame = frame;
    fte->upage = upage;
    fte->owner = thread_current();
    fte->pinned = true; // 기본적으로 핀 처리 (교체 방지)

    list_push_back(&frame_table, &fte->elem);
}

static void remove_frame_entry (void *frame){
    
}

static struct frame_entry *get_frame_entry (void *frame){
    // lock acquire
    // frame_table을 순회하면서 frame을 찾는다
    //      - 찾으면 해당 주소 return (없으면 NULL)
    // lock realease 
    return;
}

static struct frame_entry *find_evict_frame (void){
    struct frame_entry *fe;
    struct frame_entry *evict = NULL;

    // frame_table을 순회하면서 evict frame을 찾는다
    // 구현할 분량이 꽤 될 것으로 예상

    return evict;
}



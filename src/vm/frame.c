#include "frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>

struct list frame_table;       /* 프레임 테이블 */

struct lock frame_table_lock;  /* 동기화 락 */


/* functionality to manage frame_table */
static bool add_frame_entry (void *frame);
static void remove_frame_entry (void *frame);
static struct frame_entry *get_frame_entry (void *frame);
static struct frame_entry *find_evict_frame (void);
static bool save_evicted_frame (struct frame_entry *);


void frame_init(void){
    list_init(&frame_table);          /* 프레임 테이블 초기화 */
    lock_init(&frame_table_lock);     /* 락 초기화 */
}

void * frame_alloc (enum palloc_flags flags) {
    void *frame = NULL;

    frame = palloc_get_page(flags);

    /* Error handling */
    if(frame == NULL){
        // 실패 시, evict frame을 통해 기존 프레임을 교체한 후 프레임 확보
    }

    add_frame(frame);
    return frame;
}

void frame_free(void *frame){
    // remove_frame(frame);
    // palloc_free_page(frame);
}

void set_frame(void* frame){

}

/* Frame 교체 함수 */
void *evict_frame(void){
    struct frame_entry *fe;

    fe = find_evict_frame();
}

static bool add_frame_entry (void *frame){
    // struct frame_entry fe = calloc(1, sizeof(*fe));

    // if(fe == NULL)
    //     return false;

    // fe->frame = frame;
    // fe->tid = thread_current()->tid;

    // lock acquire
    // list_push_back(frame_table, &fe->elem);
    // lock realease    
}

static void remove_frame_entry (void *frame){
    // lock acquire
    // frame_table을 순회하면서 frame을 찾는다
    //      - 만약 찾으면 list_remove()하고 free
    // lock realease  
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



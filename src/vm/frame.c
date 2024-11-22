#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>

struct list frame_table;       
struct lock frame_lock;  


/* functionality to manage frame_table */
static bool frame_table_add_entry (void *frame, void *upage);
static struct frame_table_entry *frame_table_get_entry (void *frame);
static struct frame_table_entry *frame_table_find_victim (void);
static bool swap_out_evicted_page (struct frame_table_entry *victim_entry);
// static void frame_table_remove_entry (void *frame);


void frame_table_init(void)
{
    list_init(&frame_table);    
    lock_init(&frame_lock);     
}

void *frame_allocate(enum palloc_flags flags, void *upage)
{    
    void *frame = NULL;
    frame = palloc_get_page(flags);

    if(frame != NULL)
        frame_table_add_entry(frame, upage);
    else { // Frame allocation 실패 시, Evict Frame 호출       
        frame = frame_evict();
        if (frame == NULL) // Eviction도 실패한 경우 NULL 반환          
            return NULL;        
    }    

    return frame;
}

void frame_deallocate(void *frame)
{
    struct frame_table_entry *fte;
    struct list_elem *e;

    lock_acquire(&frame_lock);
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))    {
        fte = list_entry(e, struct frame_table_entry, elem);
        if (fte->frame == frame) {
            list_remove(e); // Frame Table에서 제거
            free(fte);      // Frame Table Entry 해제
            break;
        }
    }
    lock_release(&frame_lock);

    palloc_free_page(frame); // 물리 메모리 반환
}

// void frame_update_entry(void* frame){ }

void *frame_evict(void) /* Frame 교체 함수 */
{
    struct frame_table_entry *victim_entry;

    // victim_entry = frame_table_find_victim();

    // swap_out_evicted_page(victim_entry);
    
    return NULL;
}

/* Static function definition */
static bool frame_table_add_entry(void *frame, void *upage)
{
    struct frame_table_entry *fte = malloc(sizeof(struct frame_table_entry));
    
    if (fte == NULL) 
        return false;          
    
    fte->frame = frame;
    fte->upage = upage;
    fte->owner = thread_current();
    fte->pinned = true;  // 기본적으로 핀 처리 (교체 방지) : 추후 swap 구현 후 수정 예정

    lock_acquire(&frame_lock);
    list_push_back(&frame_table, &fte->elem);
    lock_release(&frame_lock);

    return true;
}

static struct frame_table_entry *frame_table_get_entry (void *frame)
{
    struct frame_table_entry *fte;
    struct list_elem *e;
    bool success = false;
    
    lock_acquire(&frame_lock);
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))    {
        fte = list_entry(e, struct frame_table_entry, elem);
        if (fte->frame == frame) {
            success = true;
            break;
        }          
    }
    lock_release(&frame_lock);
    
    return success ? fte : NULL;
}

static struct frame_table_entry *frame_table_find_victim (void){
    struct frame_entry *fe;
    struct frame_entry *evict = NULL;

    // frame_table을 순회하면서 evict frame을 찾는다
    // 구현할 분량이 꽤 될 것으로 예상

    return evict;
}

static bool swap_out_evicted_page (struct frame_table_entry *victim_entry)
{

}

// static void frame_table_remove_entry (void *frame){ }

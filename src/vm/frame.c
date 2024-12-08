#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>

struct list frame_table;       
struct lock frame_lock;

/* for iterate frame_table */
struct list_elem *hand = NULL; 
#define HAND_NEXT() hand = (list_next(hand) == list_end(&frame_table)) ? list_begin(&frame_table) : list_next(hand);


/* functionality to manage frame_table */
static bool frame_table_add_entry (void *frame, void *upage);
static struct frame_table_entry *frame_table_find_victim (void);
static bool swap_out_evicted_page (struct frame_table_entry *victim_entry);



void frame_table_init(void)
{
    list_init(&frame_table);    
    lock_init(&frame_lock);     
}

void *frame_allocate(enum palloc_flags flags, void *upage)
{    
    void *frame = NULL;
    frame = palloc_get_page(flags);
    
    /* When frame is full */
    if(frame == NULL){
        if (!frame_evict()) 
            ASSERT(false);
        frame = palloc_get_page(flags);
    }

    frame_table_add_entry(frame, upage);
    return frame;
}

void frame_deallocate(void *frame)
{
    frame_table_find_entry_delete(frame);
    palloc_free_page(frame); 
}

bool frame_table_find_entry_delete(void *frame)
{
    struct frame_table_entry *fte;
    struct list_elem *e;
    bool success = false;

    lock_acquire(&frame_lock);
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
        fte = list_entry(e, struct frame_table_entry, elem);
        if (fte->frame == frame) {
            success = true;
            list_remove(e);
            free(fte);
            break;
        }
    }
    lock_release(&frame_lock);

    return success;
}

bool frame_evict(void) 
{
    struct frame_table_entry *victim_entry;
    victim_entry = frame_table_find_victim();
    return swap_out_evicted_page(victim_entry);
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
    fte->pinned = false;  // 기본적으로 핀 False 처리 (교체 방지)

    lock_acquire(&frame_lock);
    list_push_back(&frame_table, &fte->elem);
    lock_release(&frame_lock);

    return true;
}

static struct frame_table_entry *frame_table_find_victim(void)
{
    struct frame_table_entry *victim = NULL; // Dirty 페이지 중 임시 후보 저장용
    struct list_elem *start = hand;          // 순회 시작점 저장
    struct frame_table_entry *current_entry;
    bool accessed, dirty;


    lock_acquire(&frame_lock); // frame_table 보호를 위해 lock 설정
    
    do /* 첫 번째 순회 : Reference Bit 확인 */
    { 
        if(hand == NULL) 
            hand = list_begin(&frame_table);

        current_entry = list_entry(hand, struct frame_table_entry, elem);
        
        // pinned인 경우 Pasee
        if (current_entry->pinned) {
            HAND_NEXT();
            continue;
        }

        // Check Reference, Dirty Bit
        accessed = pagedir_is_accessed(current_entry->owner->pagedir, current_entry->upage);
        dirty = pagedir_is_dirty(current_entry->owner->pagedir, current_entry->upage);

        if (!accessed && !dirty) { // Reference Bit = 0, Dirty Bit = 0: 즉시 리턴
            HAND_NEXT();
            lock_release(&frame_lock);
            return current_entry;
        }
        else if (!accessed && dirty && victim == NULL) // Reference Bit = 0, Dirty Bit = 1: victim 후보 저장
            victim = current_entry;
        
        // Reference Bit가 1이면 0으로 초기화 후 다음 프레임으로 이동
        if (accessed)
            pagedir_set_accessed(current_entry->owner->pagedir, current_entry->upage, false);

        // hand를 다음 프레임으로 이동 (원형 큐처럼 동작)
        HAND_NEXT();
    
    } while (hand != start); // 한 바퀴 순회 완료

    // 첫 번째 순회 후 Dirty victim 반환
    if (victim != NULL) {
        lock_release(&frame_lock);
        return victim;
    }
    
    start = hand;
    do /* 두 번째 순회 */
    {
        current_entry = list_entry(hand, struct frame_table_entry, elem);

        // pinned인 경우 Pasee
        if (current_entry->pinned) {
            HAND_NEXT();
            continue;
        }

        // Reference Bit와 Dirty Bit 가져오기
        dirty = pagedir_is_dirty(current_entry->owner->pagedir, current_entry->upage);

        if (!dirty) { // Reference Bit = 0, Dirty Bit = 0: 즉시 리턴            
            hand = start;
            lock_release(&frame_lock);
            return current_entry;
        }
        else if (victim == NULL) // Reference Bit = 0, Dirty Bit = 1: victim 후보 저장
            victim = current_entry;
        
        HAND_NEXT();

    } while (hand != start); // 두 바퀴 순회 완료
    
    hand = start;        
    lock_release(&frame_lock);
    return victim;
    
    /* Not Reached : 두 바퀴 순회에도 적절한 프레임을 찾지 못한 경우는 발생하지 않음 */
}

static bool swap_out_evicted_page (struct frame_table_entry *victim_entry)
{
    ASSERT(victim_entry != NULL)

    void *frame = victim_entry->frame;          
    void *upage = victim_entry->upage;          
    struct thread *owner = victim_entry->owner; 

    if (frame == NULL || upage == NULL || owner == NULL)
        PANIC("Invalid victim frame state!");

    size_t swap_index = swap_out(frame);
    struct spt_entry *spte = spt_find_entry(&owner->spt, upage);
    if (spte == NULL)
        PANIC("SPT entry not found for evicted page!");

    spte->status = PAGE_SWAP;      // 페이지 상태를 PAGE_SWAP으로 변경
    spte->swap_index = swap_index; // 스왑 인덱스 저장

    pagedir_clear_page(owner->pagedir, upage);
    frame_deallocate(frame);
    return true;
}

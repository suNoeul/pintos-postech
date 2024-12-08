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
struct list_elem *hand = NULL; // frame_table 순회를 위한 커서

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
    
    // if(frame == NULL){
    //     if (!frame_evict()) // Eviction도 실패한 경우 NULL 반환
    //         ASSERT(false);
    //     frame = palloc_get_page(flags);
    // }
    // frame_table_add_entry(frame, upage);

    if(frame != NULL)
        frame_table_add_entry(frame, upage);
    else { // Frame allocation 실패 시, Evict Frame 호출       
        if (!frame_evict()) // Eviction도 실패한 경우 NULL 반환
            ASSERT(false);
        frame = palloc_get_page(flags);
        frame_table_add_entry(frame, upage);
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

bool frame_evict(void) 
{
    struct frame_table_entry *victim_entry;
    victim_entry = frame_table_find_victim();
    ASSERT(victim_entry != NULL);
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
    struct list_elem *start = hand;          // 순회의 시작점을 저장
    struct frame_table_entry *current_entry;
    bool accessed, dirty;

    lock_acquire(&frame_lock); // frame_table 보호를 위해 lock 설정
    
    do /* 첫 번째 순회 */
    { 
        if(hand == NULL) 
            hand = list_begin(&frame_table);
        current_entry = list_entry(hand, struct frame_table_entry, elem);
        
        // pinned 여부를 확인하고, pinned된 경우 건너뜀
        if (current_entry->pinned) {
            hand = list_next(hand) == list_end(&frame_table) ? list_begin(&frame_table) : list_next(hand);
            continue;
        }

        // Reference Bit와 Dirty Bit 가져오기
        ASSERT(current_entry->owner != NULL);
        ASSERT(current_entry->owner->pagedir != NULL);
        accessed = pagedir_is_accessed(current_entry->owner->pagedir, current_entry->upage);
        dirty = pagedir_is_dirty(current_entry->owner->pagedir, current_entry->upage);

        if (!accessed && !dirty) { // Reference Bit = 0, Dirty Bit = 0: 즉시 리턴
            hand = list_next(hand) == list_end(&frame_table) ? list_begin(&frame_table) : list_next(hand);
            lock_release(&frame_lock);
            return current_entry;
        }
        else if (!accessed && dirty && victim == NULL) // Reference Bit = 0, Dirty Bit = 1: victim 후보 저장
            victim = current_entry;
        

        // Reference Bit가 1이면 0으로 초기화 후 다음 프레임으로 이동
        if (accessed)
            pagedir_set_accessed(current_entry->owner->pagedir, current_entry->upage, false);

        // hand를 다음 프레임으로 이동 (원형 큐처럼 동작)
        hand = list_next(hand) == list_end(&frame_table) ? list_begin(&frame_table) : list_next(hand);
    
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

        // pinned 여부를 확인하고, pinned된 경우 건너뜀
        if (current_entry->pinned) {
            hand = list_next(hand) == list_end(&frame_table) ? list_begin(&frame_table) : list_next(hand);
            continue;
        }

        // Reference Bit와 Dirty Bit 가져오기
        accessed = pagedir_is_accessed(current_entry->owner->pagedir, current_entry->upage);
        dirty = pagedir_is_dirty(current_entry->owner->pagedir, current_entry->upage);

        if (!accessed && !dirty) {
            // Reference Bit = 0, Dirty Bit = 0: 즉시 리턴
            hand = start;
            lock_release(&frame_lock);
            return current_entry;
        }
        else if (!accessed && dirty && victim == NULL) // Reference Bit = 0, Dirty Bit = 1: victim 후보 저장
            victim = current_entry;
        

        // hand를 다음 프레임으로 이동 (원형 큐처럼 동작)
        hand = list_next(hand) == list_end(&frame_table) ? list_begin(&frame_table) : list_next(hand);

    } while (hand != start); // 두 바퀴 순회 완료
    
    hand = start;    
    
    lock_release(&frame_lock);
    return victim;
    
    /* Not Reached : 두 바퀴 순회에도 적절한 프레임을 찾지 못한 경우는 발생하지 않음 */
}

bool frame_table_find_entry_delete(void *kpage)
{
    struct frame_table_entry *fte;
    struct list_elem *e;
    bool success = false;
    lock_acquire(&frame_lock);
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        fte = list_entry(e, struct frame_table_entry, elem);
        if (fte->frame == kpage)
        {
            success = true;
            list_remove(e);
            free(fte);
            break;
        }
    }
    lock_release(&frame_lock);

    return success;
}

static bool swap_out_evicted_page (struct frame_table_entry *victim_entry)
{
    ASSERT(victim_entry != NULL)
    void *frame = victim_entry->frame; // 물리 메모리 프레임 주소
    void *upage = victim_entry->upage; // 사용자 가상 메모리 주소
    struct thread *owner = victim_entry->owner; // 해당 프레임 소유 스레드

    if (frame == NULL || upage == NULL || owner == NULL)
        PANIC("Invalid victim frame state!");

    size_t swap_index = swap_out(frame);
    struct spt_entry *spte = spt_find_page(&owner->spt, upage);
    if (spte == NULL)
        PANIC("SPT entry not found for evicted page!");

    spte->status = PAGE_SWAP;      // 페이지 상태를 PAGE_SWAP으로 변경
    spte->swap_index = swap_index; // 스왑 인덱스 저장

    pagedir_clear_page(owner->pagedir, upage);
    frame_deallocate(frame);
    return true;
}

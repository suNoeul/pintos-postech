#include "vm/swap.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "devices/block.h"

/* Swap Table 전역 변수 */
struct swap_table swap_table;
static struct lock swap_lock;


void swap_table_init(void)
{
    /* Swap Disk 초기화 */
    swap_table.swap_disk = block_get_role(BLOCK_SWAP);  // Swap 역할로 지정된 블록 디바이스를 가져옴
    if (swap_table.swap_disk == NULL)                   // 가져온 블록 디바이스가 NULL인 경우 시스템 종료
        PANIC("No swap partition found!");

    /* Swap Bitmap 초기화 */
    size_t swap_size = block_size(swap_table.swap_disk) / (PGSIZE / BLOCK_SECTOR_SIZE); 
    swap_table.slot_count = swap_size;                  // 슬롯 개수만큼 비트맵 생성
    swap_table.used_slots = bitmap_create(swap_size);
    if (swap_table.used_slots == NULL)
        PANIC("Failed to create swap table!");

    /* 모든 슬롯을 비어 있음으로 초기화 */
    bitmap_set_all(swap_table.used_slots, false);
    lock_init(&swap_lock);
}

size_t swap_out(const void *frame)
{
    /* 사용 가능한 Swap Slot 찾기 */
    ASSERT(!lock_held_by_current_thread(&swap_lock));
    lock_acquire(&swap_lock);
    size_t slot_idx = bitmap_scan_and_flip(swap_table.used_slots, 0, 1, false);
    lock_release(&swap_lock);

    if (slot_idx == BITMAP_ERROR)
        PANIC("No available swap slots!");
    swap_table.slot_count--;

    /* 페이지 데이터를 Swap Disk에 저장 : per Sector Unit */
    for (size_t i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++) 
        block_write(swap_table.swap_disk, slot_idx * (PGSIZE / BLOCK_SECTOR_SIZE) + i, (uint8_t *)frame + i * BLOCK_SECTOR_SIZE);
    
    /* 저장된 Swap Slot의 인덱스 반환 */
    return slot_idx;
}

void swap_in(size_t swap_index, void *frame)
{
    ASSERT(bitmap_test(swap_table.used_slots, swap_index));
    
    /* Swap Slot 데이터 복구 */
    for (size_t i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
        block_read(swap_table.swap_disk, swap_index * (PGSIZE / BLOCK_SECTOR_SIZE) + i, (uint8_t *)frame + i * BLOCK_SECTOR_SIZE);

    /* Swap Slot 비트맵 갱신 */
    ASSERT(!lock_held_by_current_thread(&swap_lock));
    lock_acquire(&swap_lock);
    swap_free_slot(swap_index);
    lock_release(&swap_lock);
}

void swap_free_slot(size_t swap_index)
{
    ASSERT(swap_index < bitmap_size(swap_table.used_slots));

    /* Bitmap의 해당 bit를 해제 */
    bitmap_set(swap_table.used_slots, swap_index, false);
    swap_table.slot_count++;
}

bool swap_is_slot_in_use(size_t swap_index)
{
    ASSERT(swap_index < bitmap_size(swap_table.used_slots));

    /* 해당 Swap Slot의 사용 여부 반환 */
    return bitmap_test(swap_table.used_slots, swap_index);
}

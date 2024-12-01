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

/* Swap Table 초기화 함수 */
void swap_table_init(void)
{
    /* Swap Disk 초기화 */
    // Swap 역할로 지정된 블록 디바이스를 가져옴
    swap_table.swap_disk = block_get_role(BLOCK_SWAP);
    // 가져온 블록 디바이스가 NULL인 경우 시스템 종료
    if (swap_table.swap_disk == NULL)
        PANIC("No swap partition found!");
    /* Swap Bitmap 초기화 */
    // Swap Disk의 크기를 기반으로 슬롯 개수를 계산
    size_t swap_size = block_size(swap_table.swap_disk) / (PGSIZE / BLOCK_SECTOR_SIZE);
    swap_table.slot_count = swap_size;
    // 슬롯 개수만큼 비트맵 생성
    swap_table.used_slots = bitmap_create(swap_size);
    // 비트맵 생성 실패 시 시스템 종료
    if (swap_table.used_slots == NULL)
        PANIC("Failed to create swap table!");
    /* 모든 슬롯을 비어 있음으로 초기화 */
    // 비트맵의 모든 비트를 false로 설정
    bitmap_set_all(swap_table.used_slots, false);
    lock_init(&swap_lock);
}

/* Swap Out 함수: 메모리 페이지를 Swap Disk로 이동 */
size_t swap_out(const void *frame)
{
    /* 사용 가능한 Swap Slot 찾기 */
    // 비어 있는 Swap Slot을 검색
    // 검색된 슬롯을 사용 중으로 설정
    lock_acquire(&swap_lock);
    size_t slot_idx = bitmap_scan_and_flip(swap_table.used_slots, 0, 1, false);
    lock_release(&swap_lock);
    if (slot_idx == BITMAP_ERROR)
        PANIC("No available swap slots!");
    swap_table.slot_count--;
    /* 페이지 데이터를 Swap Disk에 저장 */
    // 페이지의 데이터를 Swap Disk에 쓰기
    // block_write()를 사용하여 데이터를 섹터 단위로 기록
    for (size_t i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
    {
        block_write(swap_table.swap_disk, slot_idx * (PGSIZE / BLOCK_SECTOR_SIZE) + i,
                    (uint8_t *)frame + i * BLOCK_SECTOR_SIZE);
    }
    /* 저장된 Swap Slot의 인덱스 반환 */
    // 성공적으로 저장된 슬롯의 인덱스를 반환
    return slot_idx;
}

/* Swap In 함수: Swap Disk에서 메모리 페이지로 복구 */
void swap_in(size_t swap_index, void *frame)
{
    ASSERT(bitmap_test(swap_table.used_slots, swap_index));
    /* Swap Slot 데이터 복구 */
    // Swap Disk에서 지정된 Swap Slot의 데이터를 읽음
    // block_read()를 사용하여 데이터를 섹터 단위로 복구
    for (size_t i = 0; i < PGSIZE / BLOCK_SECTOR_SIZE; i++)
    {
        block_read(swap_table.swap_disk, swap_index * (PGSIZE / BLOCK_SECTOR_SIZE) + i,
                   (uint8_t *)frame + i * BLOCK_SECTOR_SIZE);
    }
    /* Swap Slot 비트맵 갱신 */
    // 해당 Swap Slot의 비트를 비어 있음으로 설정
    lock_acquire(&swap_lock);
    swap_free_slot(swap_index);
    lock_release(&swap_lock);
}

/* Swap Slot 해제 함수 */
void swap_free_slot(size_t swap_index)
{
    /* 비트맵의 해당 비트를 해제 */
    // 지정된 Swap Slot의 비트를 false로 설정
    ASSERT(swap_index < bitmap_size(swap_table.used_slots));
    bitmap_set(swap_table.used_slots, swap_index, false);
    swap_table.slot_count++;
}

/* Swap Slot 사용 여부 확인 함수 */
bool swap_is_slot_in_use(size_t swap_index)
{
    /* 해당 Swap Slot의 사용 여부 반환 */
    // 비트맵에서 해당 인덱스의 비트 상태를 반환
    ASSERT(swap_index < bitmap_size(swap_table.used_slots));
    return bitmap_test(swap_table.used_slots, swap_index);
}

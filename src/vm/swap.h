#ifndef SWAP_H
#define SWAP_H

#include "devices/block.h"
#include <bitmap.h>

/* Swap 관련 정의 */
// #define SWAP_ERROR SIZE_MAX          /* Swap 에러 시 반환값 */
// #define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE) /* 한 페이지가 차지하는 섹터 수 */

/* Swap Table 구조체 */
struct swap_table {
    struct bitmap *used_slots;   /* Swap slot의 사용 여부를 관리하는 비트맵    */
    struct block *swap_disk;     /* Swap disk를 나타내는 블록 장치            */
    size_t slot_count;           /* Swap disk에 저장 가능한 총 swap slot의 수 */
};

/* Swap Table 전역 변수 */
extern struct swap_table swap_table;


/* Swap Table 초기화 함수 */
void swap_table_init(void);

/* Swap In/Out 함수 */
size_t swap_out(const void *frame);             
void swap_in(size_t swap_index, void *frame);   

/* Swap Slot 초기화 및 관리 함수 */
void swap_free_slot(size_t swap_index);         
bool swap_is_slot_in_use(size_t swap_index);    

#endif /* SWAP_H */

#ifndef PAGE_H
#define PAGE_H

#include "threads/thread.h"
#include "filesys/file.h" // `off_t`와 파일 관련 정의 포함

/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

/* PAGE Status define */
#define PAGE_FILE 1    // 파일에서 로드해야 하는 페이지
#define PAGE_PRESENT 2 // 현재 메모리에 있는 페이지
#define PAGE_SWAP 3    // 스왑 디스크에 저장된 페이지
#define PAGE_ZERO 4    // 0으로 초기화된 페이지
#define PAGE_MMAP 5    // File memory mapped

struct spt_entry{
    struct file *file;          // Excute file pointer
    void *upage;                // User Virtual Address
    off_t ofs;                  // File offset
    size_t page_read_bytes;     // 파일에서 읽어야 할 바이트 수
    size_t page_zero_bytes;     // 0으로 초기화할 바이트 수
    bool writable;              // 페이지 쓰기 가능 여부
    int status;                 
    size_t swap_index;          //평상시에는 사용안되다가 PAGE_SWAP status에서만 사용됨.
    struct hash_elem hash_elem;
};
struct mmt_entry {
    /* mmap 관련 멤버 */
    struct file *file;          // Excute file pointer
    void *upage;                // User Virtual Address
    mapid_t mmap_id;            // mmap 호출과 연결된 ID
    int page_cnt;               // Page 개수
    struct hash_elem hash_elem;
};              

/* init & management func */
void spt_init(struct hash *spt);
void spt_destroy(struct hash *spt);
void spt_destructor(struct hash_elem *e, void *aux UNUSED);

/* func of manage SPT entry */
struct spt_entry *spt_find_page(struct hash *spt, void *upage);
void spt_remove_page(struct hash *spt, void *upage);
void spt_cleanup_partial(struct hash *spt, void *upage_start) ;
bool spt_add_page(struct hash *spt, void *upage, struct file *file,
                  off_t ofs, size_t page_read_bytes, size_t page_zero_bytes, bool writable, int status);

/* SPT entry hash func */
unsigned spt_hash_func(const struct hash_elem *e, void *aux);
bool spt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux);

#endif /* PAGE_H */
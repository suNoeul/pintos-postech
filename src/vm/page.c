#include "vm/page.h"
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "lib/user/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"

void spt_init(struct hash *spt)
{
    hash_init(spt, spt_hash_func, spt_less_func, NULL);
}

void mmt_init(struct hash *mmt) 
{
    hash_init(mmt, mmt_hash_func, mmt_less_func, NULL);
}

unsigned mmt_hash_func(const struct hash_elem *e, void *aux)
{
    const struct mmt_entry *entry = hash_entry(e, struct mmt_entry, hash_elem);
    return hash_bytes(&entry->mmap_id, sizeof(entry->mmap_id));
}

bool mmt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
    const struct mmt_entry *entry_a = hash_entry(a, struct mmt_entry, hash_elem);
    const struct mmt_entry *entry_b = hash_entry(b, struct mmt_entry, hash_elem);
    return entry_a->mmap_id < entry_b->mmap_id;
}

void spt_destroy(struct hash *spt)
{
    hash_destroy(spt, spt_destructor);
}

void mmt_destroy(struct hash *mmt)
{
    hash_destroy(mmt, mmt_destructor);
}

void spt_destructor(struct hash_elem *e, void *aux UNUSED)
{
    struct spt_entry *entry = hash_entry(e, struct spt_entry, hash_elem);
    uint32_t *pagedir = thread_current()->pagedir;
    if(entry) {
        if(entry->status == PAGE_PRESENT) {
            ASSERT(pagedir != NULL);
            void *frame = pagedir_get_page(pagedir, entry->upage);
            struct frame_table_entry *fte;
            struct list_elem *e;
            for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
            {
                fte = list_entry(e, struct frame_table_entry, elem);
                if (fte->frame == frame)
                {
                    if(e == hand)
                        hand = list_remove(e);      // Frame Table에서 제거
                    else
                        list_remove(e);
                    palloc_free_page(frame); // 물리 메모리 반환
                    pagedir_clear_page(pagedir, entry->upage);
                    free(fte);               // Frame Table Entry 해제
                    break;
                }
            }
        }
        else if(entry->status == PAGE_SWAP) {
            ASSERT(entry->swap_index != -1);
            swap_free_slot(entry->swap_index);
            entry->swap_index = -1;
        }
    }
    free(entry);
}

void mmt_destructor(struct hash_elem *e, void *aux UNUSED)
{
    struct spt_entry *entry = hash_entry(e, struct mmt_entry, hash_elem);
    uint32_t *pagedir = thread_current()->pagedir;
    if (entry)
    {
        if (entry->status == PAGE_PRESENT)
        {
            ASSERT(pagedir != NULL);
            frame_pin(pagedir_get_page(pagedir, entry->upage));
        }
        if (entry->status == PAGE_SWAP)
        {
            swap_free_slot(entry->swap_index);
            entry->swap_index = -1;
        }
    }
    free(entry);
}

struct spt_entry *spt_find_page(struct hash *spt, void *upage)
{
    struct spt_entry entry;
    struct hash_elem *e;

    entry.upage = pg_round_down(upage);
    e = hash_find(spt, &entry.hash_elem);

    return e != NULL ? hash_entry(e, struct spt_entry, hash_elem) : NULL;
}

void spt_remove_page(struct hash *spt, void *upage)
{
    struct spt_entry entry;
    struct hash_elem *e;

    entry.upage = upage;
    e = hash_delete(spt, &entry.hash_elem);
    

    if (e != NULL) {
        struct spt_entry *entry = hash_entry(e, struct spt_entry, hash_elem);
        free(entry);
    }
         
}

void spt_cleanup_partial(struct hash *spt, void *upage_start) 
{
    struct hash_iterator it;
    struct spt_entry *entry;
    hash_first(&it, spt);

    while (hash_next(&it)) {
        entry = hash_entry(hash_cur(&it), struct spt_entry, hash_elem);

        // 현재 페이지가 upage_start 이후의 주소인지 확인
        if (entry->upage >= upage_start) {
            hash_delete(spt, &entry->hash_elem);
            free(entry); 
        }
    }
}

bool spt_add_page(struct hash *spt, void *upage, struct file *file,
                  off_t ofs, size_t page_read_bytes, size_t page_zero_bytes, bool writable, int status)
{
    struct spt_entry *entry = malloc(sizeof(struct spt_entry));
    
    if (entry == NULL)
        return false;

    entry->upage = upage;
    entry->file = file;
    entry->ofs = ofs;
    entry->page_read_bytes = page_read_bytes;
    entry->page_zero_bytes = page_zero_bytes;
    entry->status = status;
    entry->writable = writable;
    entry->swap_index = 0;

    struct hash_elem *result = hash_insert(spt, &entry->hash_elem);
    return result == NULL; // NULL 반환 시 성공적으로 삽입된 것
}

unsigned spt_hash_func(const struct hash_elem *e, void *aux)
{
    const struct spt_entry *entry = hash_entry(e, struct spt_entry, hash_elem);
    return hash_bytes(&entry->upage, sizeof(entry->upage));
}

bool spt_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
    const struct spt_entry *entry_a = hash_entry(a, struct spt_entry, hash_elem);
    const struct spt_entry *entry_b = hash_entry(b, struct spt_entry, hash_elem);
    return entry_a->upage < entry_b->upage;
}

struct mmt_entry *mmt_find_entry(struct hash *mmt, mapid_t *mmap_id)
{
    struct mmt_entry entry;
    struct hash_elem *e;
    entry.mmap_id = mmap_id;
    e = hash_find(mmt, &entry.hash_elem);

    return e != NULL ? hash_entry(e, struct mmt_entry, hash_elem) : NULL;
}

bool mmt_add_page(struct hash* mmt, mapid_t id, struct file *file, void *upage)
{
    struct mmt_entry *entry = malloc(sizeof (struct mmt_entry));
    entry->mmap_id = id;
    entry->file = file;
    entry->upage = upage;
    off_t ofs;
    struct hash *spt = &thread_current()->spt;
    int size = file_length(file);

    for (ofs = 0; ofs < size; ofs += PGSIZE)
    {
        if(spt_find_page(spt, upage)){
            return false;
        }
    }
    size_t page_read_bytes, page_zero_bytes;

    for (ofs = 0; ofs < size; ofs += PGSIZE)
    {
        page_read_bytes = ofs + PGSIZE < size ? PGSIZE : size - ofs;
        page_zero_bytes = PGSIZE - page_read_bytes;
        spt_add_page(spt, upage, file, ofs, page_read_bytes, page_zero_bytes, true, PAGE_FILE);
        upage += PGSIZE;
    }
    struct hash_elem *result = hash_insert(mmt, &entry->hash_elem);
    return result == NULL; // NULL 반환 시 성공적으로 삽입된 것
}

struct mmt_entry *mmt_find_by_file_and_addr(struct hash *mmt, struct file *file, void *addr)
{
    struct hash_iterator it;
    struct mmt_entry *entry;

    hash_first(&it, mmt);
    while (hash_next(&it))
    {
        entry = hash_entry(hash_cur(&it), struct mmt_entry, hash_elem);


        if (entry->file == file)
        {
            if (entry->upage <= addr && addr < entry->upage + file_length(entry->file))
            {
                return entry; // 중복 매핑 발견
            }
        }
    }

    return NULL; // 중복 없음
}
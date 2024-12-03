#include "vm/page.h"
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"

/* for Supplemental Page Table */
void spt_init(struct hash *spt)
{
    hash_init(spt, spt_hash_func, spt_less_func, NULL);
}

void spt_destroy(struct hash *spt)
{
    hash_destroy(spt, spt_destructor);
}

void spt_destructor(struct hash_elem *e, void *aux UNUSED)
{
    struct spt_entry *entry = hash_entry(e, struct spt_entry, hash_elem);
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

    if (e != NULL) 
        free(hash_entry(e, struct spt_entry, hash_elem));    
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


/* for File Memory Mapping Table */
void mmt_destroy(struct hash *mmt)
{
    hash_destroy(mmt, mmt_destructor);
}

void mmt_destructor(struct hash_elem *e, void *aux UNUSED)
{
    struct spt_entry *entry = hash_entry(e, struct mmt_entry, hash_elem);
    free(entry);
}

struct mmt_entry *mmt_find_page(struct hash *mmt, mapid_t mapping)
{
    struct mmt_entry entry;
    struct hash_elem *e;

    entry.mmap_id = mapping;
    e = hash_find(mmt, &entry.hash_elem);

    return e != NULL ? hash_entry(e, struct mmt_entry, hash_elem) : NULL;
}

bool mmt_add_page(struct mmt_entry *mmap, void *addr, int length, struct file *file)
{
    struct thread *cur = thread_current();
    int ofs = 0, page_cnt = 0;

    for (page_cnt; length > 0; page_cnt++) {
        size_t read_bytes = length < PGSIZE ? length : PGSIZE;
        size_t zero_bytes = PGSIZE - read_bytes; 

        if (!spt_add_page(&cur->spt, addr, file, ofs, read_bytes, zero_bytes, true, PAGE_MMAP)) 
            return -1;

        ofs += PGSIZE;
        addr += PGSIZE;
        length -= PGSIZE;        
    }

    mmap->page_cnt = page_cnt;
    
    struct hash_elem *result = hash_insert(&cur->mmt, &mmap->hash_elem);
    return result == NULL; // NULL 반환 시 성공적으로 삽입된 것
}
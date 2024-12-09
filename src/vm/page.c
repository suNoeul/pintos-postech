#include "vm/page.h"
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "lib/user/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"

/* SPT function Definition*/
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
    uint32_t *pagedir = thread_current()->pagedir;
    if(entry) {
        if(entry->status == PAGE_PRESENT) {
            remove_frame_entry(pagedir_get_page(pagedir, entry->upage));
        }
    }
    free(entry);
}

struct spt_entry *spt_find_entry(struct hash *spt, void *upage)
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
        struct spt_entry *removed_entry = hash_entry(e, struct spt_entry, hash_elem);
        uint32_t *pagedir = thread_current()->pagedir;

        if (removed_entry->status == PAGE_PRESENT){
            frame_deallocate (pagedir_get_page(pagedir, removed_entry->upage));
            pagedir_clear_page(pagedir, removed_entry->upage);
        }

        if (removed_entry->status == PAGE_SWAP)
            swap_free_slot(removed_entry->swap_index);        
        
        free(removed_entry);
    }         
}

void spt_cleanup_partial(struct hash *spt, void *upage_start) 
{
    struct hash_iterator it;
    struct spt_entry *entry;
    hash_first(&it, spt);

    while (hash_next(&it)) {
        entry = hash_entry(hash_cur(&it), struct spt_entry, hash_elem);

        /* 현재 페이지가 upage_start 이후의 주소인지 확인 */
        if (entry->upage >= upage_start) {
            spt_remove_page(spt, entry->upage); 
        }
    }
}

bool spt_add_page(struct hash *spt, void *upage, struct file *file,
                  off_t ofs, size_t page_read_bytes, size_t page_zero_bytes, bool writable, int status)
{
    struct spt_entry *entry = malloc(sizeof(struct spt_entry));
    if (entry == NULL) return false;

    entry->status = status;
    entry->upage = upage;
    entry->file = file;
    entry->ofs = ofs;
    entry->page_read_bytes = page_read_bytes;
    entry->page_zero_bytes = page_zero_bytes;
    entry->swap_index = 0;
    entry->writable = writable;

    struct hash_elem *result = hash_insert(spt, &entry->hash_elem);
    return result == NULL; // NULL means SUCCESS
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


/* MMT function Definition */
void mmt_init(struct hash *mmt) 
{
    hash_init(mmt, mmt_hash_func, mmt_less_func, NULL);
}

void mmt_destroy(struct hash *mmt)
{
    hash_destroy(mmt, mmt_destructor);
}

void mmt_destructor(struct hash_elem *e, void *aux UNUSED)
{
    struct spt_entry *entry = hash_entry(e, struct mmt_entry, hash_elem);
    uint32_t *pagedir = thread_current()->pagedir;
    if (entry)
    {
        if (entry->status == PAGE_PRESENT)
        {
            remove_frame_entry(pagedir_get_page(pagedir, entry->upage));
        }
    }
    free(entry);
}

struct mmt_entry *mmt_find_entry(struct hash *mmt, mapid_t mmap_id)
{
    struct mmt_entry entry;
    struct hash_elem *e;
    entry.mmap_id = mmap_id;
    e = hash_find(mmt, &entry.hash_elem);

    return e != NULL ? hash_entry(e, struct mmt_entry, hash_elem) : NULL;
}

bool mmt_check_overlap(struct hash *spt, void *addr, off_t size)
{
    for (off_t ofs = 0; ofs < size; ofs += PGSIZE) {
        if (spt_find_entry(spt, addr + ofs)) 
            return false; // When, find overlap        
    }
    return true; 
}

bool mmt_add_page(struct hash* mmt, mapid_t id, struct file *file, void *upage)
{
    struct mmt_entry *entry = (struct mmt_entry *)malloc(sizeof *entry);
    if (entry == NULL) return false;
    
    struct hash *spt = &thread_current()->spt;
    int size = file_length(file);
    size_t page_read_bytes, page_zero_bytes;
    void *start_upage = upage; 
    off_t ofs;

    entry->upage = upage;
    entry->mmap_id = id;
    entry->file = file;

    for (ofs = 0; ofs < size; ofs += PGSIZE) {
        page_read_bytes = ofs + PGSIZE < size ? PGSIZE : size - ofs;
        page_zero_bytes = PGSIZE - page_read_bytes;

        if(!spt_add_page(spt, upage, file, ofs, page_read_bytes, page_zero_bytes, true, PAGE_FILE)){
            spt_cleanup_partial(spt, start_upage);
            free(entry);
            return false;
        }
        upage += PGSIZE;
    }

    struct hash_elem *result = hash_insert(mmt, &entry->hash_elem);
    return result == NULL;
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
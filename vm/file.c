/* file.c: Implementation of memory backed file object (mmaped object). */

#include <string.h>
#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
/**
 * 파일 지원 페이지를 초기화합니다. 
 * 이 함수는 먼저 page->operations에서 파일 지원 페이지에 대한 핸들러를 설정합니다. 
 * 메모리를 지원하는 파일과 같은 페이지 구조에 대한 일부 정보를 업데이트할 수 있습니다.
 */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
    struct file_page *file_page = &page->file;

    struct file_meta_data *meta = (struct file_meta_data *)page->uninit.aux;
    file_page->file = meta->file;
    file_page->ofs = meta->ofs;
    file_page->page_read_bytes = meta->page_read_bytes;
    file_page->page_zero_bytes = meta->page_zero_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

    if (pml4_is_dirty(thread_current()->pml4, page->va))
    {
        file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->ofs);
        pml4_set_dirty(thread_current()->pml4, page->va, 0);
    }
    pml4_clear_page(thread_current()->pml4, page->va);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

    struct file *file_copy = file_reopen(file);
    void *mapped_addr = addr; 

    size_t read_bytes = file_length(file_copy) < length ? file_length(file_copy) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

    while (read_bytes > 0 || zero_bytes > 0)
    {
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct file_meta_data *meta = (struct file_meta_data *)malloc(sizeof(struct file_meta_data));
		if (meta == NULL)
			return NULL;

        meta->file = file_copy;
        meta->ofs = offset;
        meta->page_read_bytes = page_read_bytes;
        meta->page_zero_bytes = page_zero_bytes;

        if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, meta)) {
            free(meta);
			return NULL;
		}

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
    }

    return mapped_addr;
	
}

/* Do the munmap */
void 
do_munmap(void *addr) {
	/**
	 * 종료를 통하거나 다른 방법을 통해 프로세스가 exit되면 모든 매핑이 암시적으로 매핑 해제됩니다.
	 * 암시적이든 명시적이든 매핑이 매핑 해제되면 프로세스에서 쓴 모든 페이지는 파일에 다시 기록되며 기록되지 않은 페이지는 기록되지 않아야 합니다.
	 * 그런 다음 해당 페이지는 프로세스의 가상 페이지 목록에서 제거됩니다.
	 */
	while (true) {
		struct page* page = spt_find_page(&thread_current()->spt, addr);

		if (page == NULL) {
			break;
		}

		if (pml4_is_dirty(thread_current()->pml4, page->va)) {
			struct file_page *file_page = &page->file;
			file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->ofs);
			pml4_set_dirty(thread_current()->pml4, page->va, 0);
		}
		pml4_clear_page(thread_current()->pml4, page->va);
		addr += PGSIZE;
	}
}

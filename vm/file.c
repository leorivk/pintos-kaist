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
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
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
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

	/**
	 * 파일을 닫거나 제거해도 해당 매핑이 매핑 해제되지 않습니다.
	 * 생성된 매핑은 Unix 규칙에 따라 munmap이 호출되거나 프로세스가 종료될 때까지 유효합니다. 
	 * 각 매핑에 대해 파일에 대한 개별적이고 독립적인 참조를 얻으려면 file_reopen 함수를 사용해야 합니다.
	*/
	struct file *file_copy = file_reopen(file);

	if (file_copy == NULL)
		return NULL;

	void *mapped_addr = addr;

	size_t read_bytes = file_length(f) < length ? file_length(f) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct file_meta_data *meta = malloc(sizeof(struct file_meta_data));
		if (meta == NULL)
			return false;
        
        meta->file = file;
        meta->page_read_bytes = page_read_bytes;
		meta->page_zero_bytes = page_zero_bytes;
        meta->ofs = offset;

		if (!vm_alloc_page_with_initializer (VM_ANON, addr, writable, lazy_load_segment, meta)) 
			return NULL;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		offset += page_read_bytes;
		addr += PGSIZE;
	}
	return mapped_addr;
	
}

/* Do the munmap */
void
do_munmap (void *addr) {
}

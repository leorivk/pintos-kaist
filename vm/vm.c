/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{
	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *p = (struct page *)malloc(sizeof(struct page));
		bool (*page_initializer)(struct page *, enum vm_type, void *);

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			page_initializer = anon_initializer;
			break;
		case VM_FILE:
			page_initializer = file_backed_initializer;
			break;
		}

		uninit_new(p, upage, init, type, aux, page_initializer);
		p->writable = writable;

		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, p);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = NULL;

	/* TODO: Fill this function. */
	page = (struct page *)malloc(sizeof(struct page));
	struct hash_elem *e;

	page->va = pg_round_down(va); 
	e = hash_find(&spt->spt_hash, &page->hash_elem);
	free(page);

	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	/* TODO: Fill this function. */
	struct hash_elem *e = hash_insert(&spt->spt_hash, &page->hash_elem);
	return (e == NULL);}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void)
{
    struct frame *frame = malloc(sizeof(struct frame)); // 가상 메모리에 할당 -> 페이지
    if (frame == NULL) {
        PANIC("Failed to allocate memory for frame.");
    }

    frame->kva = palloc_get_page(PAL_USER); // 물리 메모리에 할당 -> 프레임
    if (frame->kva == NULL) {
        // Handle page eviction if no page is available.
        PANIC("Need to implement page eviction.");
    }

	frame->page = NULL;

    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);

    return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
	vm_alloc_page(VM_ANON | VM_MARKER_0, addr, 1);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = spt_find_page(spt, addr);
	/* TODO: Validate the fault */
	if (addr == NULL || is_kernel_vaddr(addr))
		return false;

	/* 쓰기 권한 확인 */
	if (write && page != NULL && !page->writable)
		return false;

	/**
	 * 예외로 인해 유저 모드에서 커널 모드로 전환될 때에만 스택 포인터 저장
	 * 따라서, page_fault()로 전달된 interrupt frame에서 rsp를 읽으면
	 * 유저 스택 포인터가 아닌 정의되지 않은 값 얻을 가능성 존재
	*/
	void *rsp = !user ? thread_current()->rsp : f->rsp;
	if (not_present) 
	{
		/* 프레임 할당 실패 시 */
		if (!vm_claim_page(addr)) {
			/* 스택 증가로 Page Fault를 처리할 수 있는 경우 */
			if (rsp - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK) {
				void* round_addr = pg_round_down(addr);
				vm_stack_growth(round_addr);
				if (vm_claim_page(round_addr)) // 스택 확장 후 다시 프레임 할당
					return true;
			}
			return false; 
		}
		return true; // 프레임 할당 성공
	}
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL) return false;
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *current = thread_current();
	pml4_set_page(current->pml4, page->va, frame->kva, page->writable);

	return swap_in(page, frame->kva); // uninit_initialize
}

/* Returns a hash value for page p. */
unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool page_less(const struct hash_elem *a_,
			   const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst 
 *
 * __do_fork에서 호출
 */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator iter;

	hash_first(&iter, &src->spt_hash);

	while (hash_next(&iter))
	{
		struct page *src_page = hash_entry(hash_cur(&iter), struct page, hash_elem);
		enum vm_type type = src_page->operations->type;
		void *upage = src_page->va;
		bool writable = src_page->writable;

		if (type == VM_UNINIT)
		{
			vm_initializer *init = src_page->uninit.init;
			void *aux = src_page->uninit.aux;
			if (!vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, aux))
				return false;
				
			continue;
		}

		// if (type == VM_FILE)
		// {

		// }

		if (!vm_alloc_page(type, upage, writable)) 
			return false;

		if (!vm_claim_page(upage))
			return false;

		struct page *dst_page = spt_find_page(dst, upage);
		if (dst_page == NULL)
			return false;

		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
	}

	return true;

}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->spt_hash, spt_hash_destroy); // hash_destroy : 해시 테이블까지 삭제
}

void spt_hash_destroy(struct hash_elem *e, void *aux)
{
	struct page *page = hash_entry(e, struct page, hash_elem);
	destroy(page);
	free(page);
}
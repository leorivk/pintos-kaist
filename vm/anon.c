/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

struct bitmap *swap_table;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	/* TODO: Set up the swap_disk. */

	/* 스왑 디스크를 가져옴 (디스크 1의 파티션 1) */
	swap_disk = disk_get(1, 1);
	/* 스왑 디스크의 총 섹터 수를 페이지 단위로 변환 */
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	/* 스왑 테이블을 초기화하여 각 페이지의 스왑 슬롯 상태 추적 
		(모든 bit들을 false로 초기화, 사용 시 true) */
	swap_table = bitmap_create(swap_size); 
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;

	int page_no = anon_page->swap_slot_no;

	/* 해당 슬롯이 사용 중인지 확인, 사용 중이 아니라면 false 반환 */
	if (bitmap_test(swap_table, page_no) == false) 
		return false;

	/* 섹터 순회 */
	for (int i=0; i < SECTORS_PER_PAGE; ++i) {
		/* 스왑 디스크에서 섹터 데이터를 읽어와 메모리에 복사 */
		disk_read(swap_disk, page_no * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);
	}

	/* 스왑 슬롯을 사용 가능한 상태로 업데이트 (false) */
	bitmap_set(swap_table, page_no, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	/* 사용 가능한 스왑 슬롯 검색 */
	int page_no = bitmap_scan(swap_table, 0, 1, false);
	/* 사용 가능한 슬롯이 없으면 BITMAP_ERROR 반환 */
	if (page_no == BITMAP_ERROR) 
		return false;
	
	/* 섹터 순회 */
	for (int i=0; i < SECTORS_PER_PAGE; ++i) {
		/* 페이지의 섹터 데이터를 스왑 디스크에 복사 */
		disk_write(swap_disk, page_no * SECTORS_PER_PAGE + i, page->va + DISK_SECTOR_SIZE * i);
	}

	/* 스왑 슬롯을 사용 중인 상태로 업데이트 (true) */
	bitmap_set(swap_table, page_no, true);
	anon_page->swap_slot_no = page_no;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
}

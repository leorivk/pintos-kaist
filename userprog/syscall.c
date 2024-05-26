#include "userprog/syscall.h"
#include <stdio.h>
#include "filesys/file.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include <devices/input.h>
#include "lib/kernel/stdio.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
bool create (const char *file, unsigned initial_size);
void halt (void);
void exit (int status);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
int fork (const char *thread_name);
int wait (int pid);
void check_addr(char *addr);

struct lock filesys_lock;

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	int syscall_n = f->R.rax; /* 시스템 콜 넘버 */
#ifdef VM
	thread_current()->rsp = f->rsp;
#endif
	switch (syscall_n)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi);
		break;
	// case SYS_EXEC:
	// 	f->R.rax = exec(f->R.rdi);
	// 	break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	// case SYS_MMAP:
	// 	f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
	// 	break;
	// case SYS_MUNMAP:
	// 	munmap(f->R.rdi);
	// 	break;
	}
}

int fork (const char *thread_name) {
	struct thread *cur = thread_current();
	return process_fork(thread_name, &cur->tf);
}

int wait (int pid) {
	return process_wait(pid);
}

void 
halt (void) {
	power_off();
}

void 
exit (int status) {
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

int
exec(const char *cmd_line) {
	tid_t tid = process_create_initd(cmd_line);
	return process_wait(tid);
}

// int exec(const char *cmd_line)
// {
// 	check_address(cmd_line);

// 	char *cmd_line_copy;
// 	cmd_line_copy = palloc_get_page(0);
// 	if (cmd_line_copy == NULL)
// 		exit(-1);							  
// 	strlcpy(cmd_line_copy, cmd_line, PGSIZE);

// 	if (process_exec(cmd_line_copy) == -1)
// 		exit(-1);
// }

bool 
create (const char *file, unsigned initial_size) {
	check_addr(file);
	return filesys_create(file, initial_size);
}

bool 
remove (const char *file) {
	check_addr(file);
	return filesys_remove(file);
}

int 
open (const char *file) {

	check_addr(file);
	struct file *open_file = filesys_open(file);

	if (open_file == NULL) 
		return -1;

	int fd = process_add_file(open_file);

	if (fd == -1)
		file_close(open_file);

	return fd;
}

int 
filesize (int fd) {
	struct file *open_file = process_get_file(fd);
	if (open_file) 
		return file_length(open_file);
	return -1;
}

int 
read (int fd, void *buffer, unsigned size) {
	int count;
	check_addr(buffer);
	unsigned char *buff = buffer;

	lock_acquire(&filesys_lock);

	if (fd == 0) {
		char key;
		for(int i = 0; i < size; i++) {
			key = input_getc();
			*buff++ = key;
			if (key == '\0') {
				count = i;
				break;
			}
		}
	} else {
		struct file *open_file = process_get_file(fd);
		count = file_read(open_file, buffer, size);
	}
	lock_release(&filesys_lock);
	return count;
}

int 
write (int fd, const void *buffer, unsigned size) {
	int count = 0;
	check_addr(buffer);
	lock_acquire(&filesys_lock);
	if (fd == 1) {
		putbuf(buffer, size);
		lock_release(&filesys_lock);
		return size;
	}
	struct file *open_file = process_get_file(fd);
	if (open_file == NULL) {
		lock_release(&filesys_lock);
		return -1;
	}
	count = file_write(open_file, buffer, size);

	lock_release(&filesys_lock);
	return count;
}

void 
seek (int fd, unsigned position) {
	struct file *open_file = process_get_file(fd);
	if (fd < FDT_PAGES || fd > FDT_COUNT_LIMIT)
		return;
	if (open_file)
		file_seek(open_file, position);
}

unsigned 
tell (int fd) {
	struct file *open_file = process_get_file(fd);
	if (fd < FDT_PAGES || fd > FDT_COUNT_LIMIT)
		return;
	if (open_file)
		return file_tell(open_file);
	return NULL;
}

void 
close (int fd) {
	struct file *open_file = process_get_file(fd);
	if (open_file == NULL)
		return;
	process_close_file(fd);
}

void 
check_addr(char *addr) {
	struct thread *cur = thread_current();
	if (addr == NULL || !is_user_vaddr(addr) || pml4_get_page(cur->pml4, addr) == NULL) exit(-1);
}

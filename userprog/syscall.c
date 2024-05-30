#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "threads/synch.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "lib/kernel/console.h"
#include "threads/palloc.h"
#include "lib/string.h"
#include "threads/mmu.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
/* add function gdy_pro2*/
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
void check_address(void *addr);
tid_t exec(const char *cmd_line);
int wait(tid_t tid);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
tid_t fork (const char *thread_name, struct intr_frame *f);

/* add function gdy_pro2*/

struct lock filesys_lock; // file에 대한 동시 접근을 방지하기 위한 lock

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
/* add function gdy_pro2*/
void syscall_handler(struct intr_frame *f UNUSED)
{
	// printf("system call!\n");
	int syscall_number = f->R.rax; // rax에 시스템 콜 넘버가 저장되어 있음
	// printf("syscall number %d\n",syscall_number);		// 10
	switch (syscall_number)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi,f);
		break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
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
	default:
		thread_exit();
		break;
	}
}
// pintos를 종료시키는 시스템 콜
void halt(void)
{
	power_off(); // 핀토스를 종료
}
// 현재 프로세스를 종료시키는 시스템 콜
void exit(int status)
{
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit(); // 쓰레드 종료
}
// 파일을 생성하는 시스템 콜 (성공일 경우 true 리턴)
bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size); // 파일 이름과 파일 사이즈를 인자 값으로 받아 파일을 생성
}
// 파일을 삭제하는 시스템 콜
bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file); // 파일 이름에 해당하는 파일을 제거
}
// 주소 값이 유저 영역에서 사용하는 주소 값인지 확인
void check_address(void *addr)
{
	if (addr == NULL || !is_user_vaddr(addr) || pml4_get_page(thread_current()->pml4, addr) == NULL)
	{
		exit(-1);
	}
}

// 자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜
tid_t exec(const char *cmd_line) // 자식  프로세스 생성 xxx????
{
	check_address(cmd_line);
	char *cmd_line_copy;
    cmd_line_copy = palloc_get_page(0);
    if (cmd_line_copy == NULL)
        exit(-1);                              // 메모리 할당 실패 시 status -1로 종료한다.
    strlcpy(cmd_line_copy, cmd_line, PGSIZE); // cmd_line을 복사한다.
    // 스레드의 이름을 변경하지 않고 바로 실행한다.
    if (process_exec(cmd_line_copy) == -1)
        exit(-1); // 실패 
}

// 자식 프로세스가 수행되고 종료될 때 까지 대기
int wait(tid_t tid)
{
	return process_wait(tid);
}

// 파일을 열 때 사용하는 시스템 콜
int open(const char *file)
{
	check_address(file);
	struct file *f = filesys_open(file); // 파일 open
	if (f == NULL)
	{
		return -1;
	}
	int fd = process_add_file(f);
	if (fd == -1)
	{
		file_close(f); // fd_table에 빈 공간이 없을 경우 파일 닫기
	}
	return fd;
}

// 파일의 크기를 알려주는 시스템 콜
int filesize(int fd)
{
	struct thread *curr = thread_current();
	if (fd < 2 || fd >= MAX_FD || curr->fd_table[fd] == NULL)
	{
		return -1; // 해당 파일이 존재하지 않는 경우 -1 return
	}
	struct file *f = curr->fd_table[fd];
	return file_length(f);
}

// 열린 파일의 데이터를 읽는 시스템 콜
int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	struct thread *curr = thread_current();
	lock_acquire(&filesys_lock); // 파일에 동시 접근이 일어날 수 있으므로 lock 사용
	if (fd < 0 || fd >= MAX_FD || buffer == NULL)
	{
		lock_release(&filesys_lock);
		return -1;
	}
	if (fd == 0)
	{ // fd가 0 일 경우 키보드에 입력을 버퍼에 저장 후 버퍼의 저장한 크기 리턴
		for (int i = 0; i < size; i++)
		{
			((char *)buffer)[i] = input_getc();
		}
		lock_release(&filesys_lock);
		return size;
	}
	struct file *f = curr->fd_table[fd];
	if (f == NULL)
	{
		lock_release(&filesys_lock);
		return -1;
	}
	int bytes_read = file_read(f, buffer, size); // fd가 0이 아닐 경우 파일의 데이터를 크기만큼 저장 후 읽은 바이트 수 리턴
	lock_release(&filesys_lock);
	return bytes_read;
}

// 열린 파일의 데이터를 기록 하는 시스템 콜
int write(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	lock_acquire(&filesys_lock); // 파일에 동시 접근이 일어날 수 있으므로 lock 사용
	struct thread *curr = thread_current();
	if (fd < 1 || fd >= MAX_FD || buffer == NULL)
	{
		lock_release(&filesys_lock);
		return -1;
	}
	if (fd == 1)
	{ // fd값이 1일 경우 버퍼에 저장된 값을 화면에 출력 후 버퍼의 크기 리턴
		putbuf((char *)buffer, size);
		lock_release(&filesys_lock);
		return size;
	}
	struct file *f = curr->fd_table[fd];
	if (f == NULL)
	{
		lock_release(&filesys_lock);
		return -1;
	}

	int bytes_written = file_write(f, buffer, size);
	lock_release(&filesys_lock);
	return bytes_written;
}

// 열린 파일의 위치(offset)를 이동하는 시스템 콜
void seek(int fd, unsigned position)
{
	if (fd < 0 || fd >= MAX_FD)
	{
		return;
	}
	struct thread *curr = thread_current();
	struct file *f = curr->fd_table[fd];
	if (f != NULL)
	{
		file_seek(f, position);
	}
}

// 열린 파일의 위치(offset)를 알려주는 시스템 콜
unsigned tell(int fd)
{
	if (fd < 0 || fd >= MAX_FD)
	{
		return;
	}
	struct thread *curr = thread_current();
	struct file *f = curr->fd_table[fd];
	if (f != NULL)
	{
		return file_tell(f);
	}
}

// 열린 파일을 닫는 시스템 콜
void close(int fd)
{
	process_close_file(fd);
}

// 자식 프로세스 생성  
tid_t fork (const char *thread_name, struct intr_frame *f){
	return process_fork(thread_name,f);
}

/* add function gdy_pro2*/

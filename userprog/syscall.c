#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/flags.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "devices/input.h"
#include "lib/kernel/stdio.h"
#include "threads/palloc.h"
#include "vm/vm.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file_name);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
tid_t fork(const char *thread_name, struct intr_frame *f);
int exec(const char *cmd_line);
int wait(int pid);


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
	/**
	 * MSR_STAR: 'SEL_UCSEG'와 'SEL_KCSEG' 값을 설정
	 * SEL_UCSEG: user code segment selector
	 * SEL_KCSEG: kernel code segment selector
	*/
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  | ((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	/**
	 * filesys_lock? 파일 시스템에 대한 동시 접근을 제어하기 위해 사용.(race condition 방지)
	*/
	lock_init(&filesys_lock);//file system lock 초기화
}

/* The main system call interface 
* rsp: stack pointer, rdi: 1st arugment, rsi: 2nd argument, rdx: 3rd argument
*/
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	//interrupt frame pointer의 stack pointer를 가져옴.
	int syscall_num = f->R.rax; //syscall 넘버
	// printf("%d\n", f->R.rax);
	switch (syscall_num)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
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
		exit(-1);//추가
		break;
	}
	// thread_exit (); ㅏㅏㅏㅏㅏ,,,,미친
}

/*
System call handler 함수
*/
//Pintos 종료시키는 syscall
void
halt(void){
	power_off();
}

//현재 프로세스를 종료시키는 syscall
void
exit(int status){
	struct thread *cur = thread_current();
	cur->exit_status = status; // exit status 저장
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

// 파일을 생성하는 syscall. file: 생성할 파일. initial_size: 생성할 파일의 크기
bool
create(const char *file, unsigned initial_size){
	check_address(file);
	return filesys_create(file, initial_size);
}

//file 이름에 해당하는 파일 지우기
bool
remove(const char* file){
	check_address(file);
	if(file == NULL) exit(-1);
	return filesys_remove(file);
}

void
check_address(void *addr){
	if(addr == NULL)exit(-1);
	if(!is_user_vaddr(addr))exit(-1);
	//pml4테이블을 이용해 가상주소를 찾을 때 없을 경우 exit
	if(pml4_get_page(thread_current()->pml4, addr) == NULL)exit(-1);
}

int open(const char *file_name)
{
	check_address(file_name);
	lock_acquire(&filesys_lock);
	struct file *file = filesys_open(file_name);
	if (file == NULL)
	{
		lock_release(&filesys_lock);
		return -1;
	}
	int fd = process_add_file(file);
	if (fd == -1)
		file_close(file);
	lock_release(&filesys_lock);
	return fd;
}

int filesize(int fd)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return -1;
	return file_length(file);
}

void seek(int fd, unsigned position)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	file_seek(file, position);
}

unsigned tell(int fd)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	return file_tell(file);
}

void close(int fd)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	file_close(file);
	process_close_file(fd);
}
int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);

	char *ptr = (char *)buffer;
	int bytes_read = 0;

	lock_acquire(&filesys_lock);//file system lock 획득
	if (fd == 0)
	{
		for (int i = 0; i < size; i++)
		{
			*ptr++ = input_getc();
			bytes_read++;
		}
		lock_release(&filesys_lock);
		return size;
	}
	else
	{
		if (fd < 2)
		{
			lock_release(&filesys_lock);
			return -1;
		}
		struct file *file = process_get_file(fd);
		if (file == NULL)
		{

			lock_release(&filesys_lock);
			return -1;
		}
		
		bytes_read = file_read(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return bytes_read;
}



int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	int bytes_write = 0;//
	if (fd == 1)
	{
		putbuf(buffer, size);
		bytes_write = size;
		return size;
	}
	else
	{
		if (fd < 2)
			return -1;
		struct file *file = process_get_file(fd);
		if (file == NULL)
			return -1;
		lock_acquire(&filesys_lock);
		bytes_write = file_write(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return bytes_write;
}

//부모 프로세스, 자식 프로세스 모두에서 호출. 부모 프로세스는 자식pid 반환, 자식은 0을 반환.
tid_t fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

//logic: 부모 프로세스가 fork를 호출, 자식 프로세스를 생성.
//자식은 fork()의 반환값을 검사, 자식에서 exec를 호출해 새로운 프로그램을 실행.
int exec(const char *cmd_line)
{
	// *cmd_line 의 유효성 검사
	check_address(cmd_line);

	// process.c 파일의 process_create_initd 함수와 유사하다.
	// 단, 스레드를 새로 생성하는 건 fork에서 수행하므로
	// 이 함수에서는 새 스레드를 생성하지 않고 process_exec을 호출한다.

	// process_exec 함수 안에서 filename을 변경해야 하므로
	// 커널 메모리 공간에 cmd_line의 복사본을 만든다.
	// 현재 const char* 타입이기 때문에 수정할 수 없음. -> char* 타입으로 변경.
	char *cmd_line_cpy;
	cmd_line_cpy = palloc_get_page(0);
	if (cmd_line_cpy == NULL)
		exit(-1);							  // 메모리 할당 실패 시 status -1로 종료한다.
	strlcpy(cmd_line_cpy, cmd_line, PGSIZE); // cmd_line을 복사한다.

	// 스레드의 이름을 변경하지 않고 바로 실행한다.
	if (process_exec(cmd_line_cpy) == -1)
		exit(-1); // 실패 시 status -1로 종료한다.
}

int wait(int pid)
{
/* 
 * thread 식별자 tid가 종료될 때까지 기다리고, exit status를 반환.
 * thread가 커널에 의해 종료되었을 경우 -1 반환. 
 * TID가 유효하지 않거나,thread가 자식 스레드가 아니거나, 해당하는 tid에 대해 process_wait()가 호출되었으면 즉시 -1 반환
 */
	return process_wait(pid);
}
#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "lib/string.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);
struct thread *get_child_process(int tid);
void remove_child_process(struct thread *cp);

/* General process initializer for initd and other process. */
static void
process_init(void)
{
	struct thread *current = thread_current();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */

/* user 영역의 첫 번째 프로그램인 "initd"를 file_name에서 로드하여 시작한다. (=process_excute)*/
tid_t process_create_initd(const char *file_name)
{
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy(fn_copy, file_name, PGSIZE); // fn_copy에 file_name copy
	/* add code - gdy_pro2*/
	char *token, *save_ptr, *new_file_name;
	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr))
	{
		new_file_name = token; // 첫번째 인자 -> 파일 이름
		break;
	}

	// printf("new_filename: %s\n", new_file_name);
	// printf("fn_copy: %s\n", fn_copy);
	/* add code - gdy_pro2*/
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create(new_file_name, PRI_DEFAULT, initd, fn_copy); // new_file_name을 매개변수로 재 설정, fn_copy 값에는 passing 되지 않은 값이 들어간다.
	if (tid == TID_ERROR)
		palloc_free_page(fn_copy);
	return tid;
}
/* add function pro2*/

// 자식리스트를 tid로 검색하여 해당 프로세스 디스크립터(thread)를 반환
struct thread *get_child_process(int tid)
{
	struct thread *t = thread_current();
	if (!tid)
	{
		return NULL;
	}
	else
	{
		struct list_elem *child_list_first = list_begin(&t->child_list);
		while (child_list_first != list_end(&t->child_list))
		{
			struct thread *first_thread = list_entry(child_list_first, struct thread, c_elem);
			if (first_thread->tid == tid)
			{
				return first_thread;
			}
			child_list_first = list_next(child_list_first);
		}
		return NULL;
	}
}
// 부모 프로세스의 자식 리스트에서 프로세스 디스크립터(thread) 제거
void remove_child_process(struct thread *cp)
{
	list_remove(&cp->c_elem);
}

// 파일 객체에 대한 파일 디스크립터 생성
int process_add_file(struct file *f)
{
	struct thread *curr = thread_current();
	for (int fd = 2; fd < MAX_FD; fd++)
	{
		if (curr->fd_table[fd] == NULL)
		{
			curr->fd_table[fd] = f; // 해당 파일 객체에 파일 디스크립터 부여
			curr->max_fd = fd + 1;
			return fd; // 파일 디스크립터 리턴
		}
	}
	return -1;
}

// 프로세스의 파일 디스크립터 테이블을 검색하여 파일 객체의 주소를 리턴
struct file *process_get_file(int fd)
{
	struct thread *t = thread_current();
	if (2 <= fd < MAX_FD)
	{
		if (t->fd_table[fd] != NULL)
		{
			return t->fd_table[fd];
		}
	}
	return NULL;
}

// 파일 디스크립터에 해당하는 파일을 닫고 해당 엔트리 초기화
void process_close_file(int fd)
{
	struct thread *t = thread_current();
	if (2 <= fd < MAX_FD)
	{
		file_close(t->fd_table[fd]);
		t->fd_table[fd] = NULL;
	}
}
/* add function pro2*/
/* A thread function that launches first user process. */
static void
initd(void *f_name)
{
#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif

	process_init();

	if (process_exec(f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
// 현재 프로세스를 name으로 복제
// 새로운 프로세스의 스레드 id를 반환, 스레드를 생성할 수 없는 경우 tid_error를 반환
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{
	/* Clone current thread to new thread.*/
	struct thread *curr = thread_current();
	struct intr_frame real_parent;
	memcpy(&real_parent, if_, sizeof(struct intr_frame)); //  parent_if(인터럽트 프레임)에 fork 함수를 통해 받는 if_를 복사한다.
	curr->parent_if = &real_parent;
	// 현재 스레드를 fork한 new 스레드를 생성
	tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, curr); // 인자로는 if_를 복사한 parent_if를 가지고 있는 스레드 = curr을 넣어줌
	if (tid == TID_ERROR)
	{
		return TID_ERROR;
	}
	// 생성된 자식 스레드 찾기
	struct thread *child = get_child_process(tid);
	sema_down(&child->sema_wait); // 로드 완료될 때까지 부모 스레드 대기
	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */

// 부모의 주소 공간을 복제하기 위해 이 함수를 pml4_for_each에 전달 - 복제가 성공적으로 되었을 때 true 반환
static bool
duplicate_pte(uint64_t *pte, void *va, void *aux) // pte: 페이지 테이블 엔트리를 가리키는 포인터 .. va : 가상 주소 포인터 .. aux : 부모 스레드를 가리키는 포인터
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	// TODO: 만약 부모 페이지가 커널 페이지라면 즉시 반환 (복제 x)
	if (is_kernel_vaddr(va))
	{
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */

	// TODO: 부모의 맵 레벨 4에서 가상주소를 해석
	parent_page = pml4_get_page(parent->pml4, va);
	if (parent_page == NULL)
	{
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	// 자식을 위해 새로운 PAL_USER 페이지를 할당하고 결과를 NEWPAGE로 설정
	newpage = palloc_get_page(PAL_USER | PAL_ZERO); // PAL_USER : 사용자 영역에 페이지를 할당 .. PAL_ZERO : 새로 할당된 페이지를 0으로 초기화
	// -> 사용자 영역에 0으로 초기화된 새 페이지를 할당하고, 그 주소를 'newpage'에 할당
	if (newpage == NULL)
	{
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	// 부모의 페이지를 새 페이지로 복제하고 부모 페이지가 쓰기 가능한지 확인 (결과에 따라 WRITABLE을 설정)
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	// WRITABLE 권한으로 주소 VA에서 자식의 페이지 테이블에 새 페이지를 추가
	if (!pml4_set_page(current->pml4, va, newpage, writable))
	{
		/* 6. TODO: if fail to insert page, do error handling. */
		// 페이지 삽입에 실패하면 오류 처리를 수행
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */

// 부모의 실행 컨텍스트를 복사하는 스레드 함수
// parent->tf는 프로세스의 userland 컨텍스트를 보유하지 않는다. 즉 process_fork의 두 번째 인수를 이 함수에 전달해야 한다.
static void
__do_fork(void *aux)
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *)aux;
	struct thread *current = thread_current();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	/* TODO: 어떻게든 부모의 인터럽트 프레임(parent_if)를 전달 (process_fork()'s의 if_)*/
	struct intr_frame *parent_if = parent->parent_if; // 부모 인터럽트 프레임 전달
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	/* cpu 컨텍스트를 로컬 스택으로 읽어 들인다.*/
	memcpy(&if_, parent_if, sizeof(struct intr_frame));
	if_.R.rax = 0; // 자식 프로세스의 리턴 값은 0
	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate(current);
#ifdef VM
	supplemental_page_table_init(&current->spt);
	if (!supplemental_page_table_copy(&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	// 파일 객체를 복제 하려면  include/filesys/file.h에 있는 file_duplicate를 사용
	// 부모는 이 함수가 부모의 리소스를 성공적으로 복제할 때까지 fork()에서 반환해서는 안된다.
	// fdt 복제  (파일 디스크립터 테이블)
	for (int i = 2; i < MAX_FD; i++)
	{
		struct file *f = parent->fd_table[i];
		if (f != NULL)
		{
			current->fd_table[i] = file_duplicate(f);
		}
	}
	current->max_fd = parent->max_fd;

	sema_up(&current->sema_wait);
	process_init();

	/* Finally, switch to the newly created process. */
	// 새로 생성된 프로세스로 전환
	if (succ)
		do_iret(&if_);
error:
	sema_up(&current->sema_wait);
	// thread_exit();
	exit(TID_ERROR);
}

/* add function - gdy_pro2*/

// parse = 프로그램 이름과 인자가 저장된 메모리, count = 인자의 개수, rsp = 스택 포인터를 가리키는 주소 값
/* user stack에 파싱된 토큰을 저장하는 함수 */
void argument_stack(char **parse, int count, void **rsp)
{
	char *arg_ptr[count]; // user_stack의 주소 저장할 것 이다.
	size_t len;
	// 파일이름과 인자 push
	for (int i = count - 1; i >= 0; i--) // parse에 담긴 인자를 뒤에서 부터 stack에 넣는다.
	{
		len = strlen(parse[i]) + 1;
		*rsp -= len;
		memcpy(*rsp, parse[i], len);
		arg_ptr[i] = *rsp;
	}

	// 정렬을 위한 패딩 추가
	uintptr_t stack_alignment = (uintptr_t)(*rsp) % 8; // stack_alignment은 8비트(1바이트)
	if (stack_alignment != 0)
	{
		*rsp -= stack_alignment;		  // padding 하나당 1바이트로 정렬을 위해 그만큼 rsp를 감소시킨다.
		memset(*rsp, 0, stack_alignment); // 늘린 rsp만큼의 stack 공간을 0으로 채운다.
	}
	// 문자열이 종료되었음을 알려주는 0 push
	*rsp -= sizeof(char *); // sizeof(char *) 은 8바이트 (아키텍처 마다 달라질 수 있음)
	*(char **)(*rsp) = 0;

	// 인자 주소 push
	for (int i = count - 1; i >= 0; i--)
	{
		*rsp -= sizeof(char *);
		*(char **)(*rsp) = arg_ptr[i];
		// printf("rsp주소가 가리키는 값\n%p\n",*rsp);
	}
	// argv 포인터를 push

	// char **argv_ptr = *rsp;
	// *rsp -= sizeof(char **);
	// *(char ***)(*rsp) = argv_ptr;

	// argc push

	// *rsp -= sizeof(int);
	// *(int *)(*rsp) = count;

	// return address(fake address)  0 push

	*rsp -= sizeof(void *);
	*(void **)(*rsp) = 0;
}
/* add function - gdy_pro2*/

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */

/* 현재 실행 컨텍스트를 f_name으로 전환한다. 실패 시 -1을 반환 (= start_process)*/
int process_exec(void *f_name)
{
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup();

	/* And then load the binary */
	/* add code - gdy_pro2*/
	/* 문자열 파싱*/
	char *token, *save_ptr;
	int count = 0;
	char *parse[128]; // 파일이름 + 인자 저장x
	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL;
		 token = strtok_r(NULL, " ", &save_ptr))
	{
		parse[count] = token;

		// parse[count] = palloc_get_page(0); // 토큰의 복사본을 저장할 메모리 할당
		// strlcpy(parse[count], token, PGSIZE); // 토큰의 내용을 복사하여 parse 배열에 저장
		// count++;
		// printf("parse1: %s \n", parse[count]);
		count++;
	}
	/* add code - gdy_pro2*/
	/* 프로그램을 메모리에 적재 */
	success = load(parse[0], &_if); // load에 파일이름만 넘겨준다.
	thread_current()->is_program_loaded = success;
	// 메모리 적재 완료 -> 부모 프로세스 다시 진행
	//

	/* add code - gdy_pro2*/
	if (!success)
	{
		palloc_free_page(file_name);
		// remove_child_process(thread_current());			// load 실패 -> 프로그램에 메모리적재 실패 시 child_list에서 삭제
		// palloc_free_page(thread_current());
		// thread_exit();		//
		return -1;
	}
	argument_stack(parse, count, &_if.rsp); // 파싱한 문자들을 담은 배열(parse), 인자의 개수(count), 스택 포인터(rsp)를 넣어준다.
	// 메모리 내용을 16진수로 화면에 출력한다. (유저 스택에 인자를 저장 후 유저 스택 메모리 확인)
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);
	_if.R.rsi = _if.rsp + sizeof(void *);
	_if.R.rdi = count;
	
	/* add code - gdy_pro2*/

	/* If load failed, quit. */
	// argument_stack으로 parse를 전달해준 이후에 palloc_free_page를 통해 file_name이 가리키는 메모리를 해제한다.
	palloc_free_page(file_name);
	/* Start switched process. */
	do_iret(&_if);
	NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait(tid_t child_tid UNUSED)
{
	struct thread *t = get_child_process(child_tid);
	if (t == NULL)
	{
		return -1;
	}
	sema_down(&t->sema_load);
	int exit_s = t->exit_status;
	remove_child_process(t); // 
	sema_up(&t->sema_exit); // 
	// palloc_free_page(t);
	// return t->exit_status;
	return exit_s;
}

/* Exit the process. This function is called by thread_exit (). */
void process_exit(void)
{
	for (int fd = 2; fd < MAX_FD; fd++)
	{
		process_close_file(fd);
	}
	file_close(thread_current()->running);		// add_code, 실행중인 파일도 닫기 (load에서 갱신)
	process_cleanup();
	sema_up(&thread_current()->sema_load);
	sema_down(&thread_current()->sema_exit); // 부모 프로세스가 ready_list에 들어갈 수 있도록 sema_up
}

/* Free the current process's resources. */
static void
process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	supplemental_page_table_kill(&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
	/* Activate thread's page tables. */
	pml4_activate(next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update(next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0			/* Ignore. */
#define PT_LOAD 1			/* Loadable segment. */
#define PT_DYNAMIC 2		/* Dynamic linking info. */
#define PT_INTERP 3			/* Name of dynamic loader. */
#define PT_NOTE 4			/* Auxiliary info. */
#define PT_SHLIB 5			/* Reserved. */
#define PT_PHDR 6			/* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
						 uint32_t read_bytes, uint32_t zero_bytes,
						 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load(const char *file_name, struct intr_frame *if_)
{
	struct thread *t = thread_current();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create();
	if (t->pml4 == NULL)
		goto done;
	process_activate(thread_current());

	/* Open executable file. */
	file = filesys_open(file_name);
	if (file == NULL)
	{
		printf("load: %s: open failed\n", file_name);
		goto done;
	}
	t->running = file;
	// // add code pro2
	file_deny_write(file);

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E // amd64
		|| ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024)
	{
		printf("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type)
		{
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/* Ignore this segment. */
			break;
		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			goto done;
		case PT_LOAD:
			if (validate_segment(&phdr, file))
			{
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0)
				{
					/* Normal segment.
					 * Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				}
				else
				{
					/* Entirely zero.
					 * Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}
				if (!load_segment(file, file_page, (void *)mem_page,
								  read_bytes, zero_bytes, writable))
					goto done;
			}
			else
				goto done;
			break;
		}
	}

	/* Set up stack. */
	if (!setup_stack(if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close(file);
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page(void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable))
		{
			printf("fail\n");
			palloc_free_page(kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack(struct intr_frame *if_)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage != NULL)
	{
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page(kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer(VM_ANON, upage,
											writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack(struct intr_frame *if_)
{
	bool success = false;
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */

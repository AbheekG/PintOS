#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include <inttypes.h>
#include <list.h>

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/io.h"

#include "devices/input.h"

#include "filesys/file.h"
#include "filesys/filesys.h"

#include "userprog/syscall.h"
#include "userprog/process.h"

typedef int pid_t;
typedef int fid_t;

static void syscall_handler (struct intr_frame *);

static 	void 	syscall_halt (void);
static 	void 	syscall_exit (int);
static 	pid_t 	syscall_exec (const char *);
static 	int 	syscall_wait (pid_t);
static 	bool 	syscall_create (const char *, unsigned );
static 	bool 	syscall_remove (const char *);
static 	int 	syscall_open (const char *);
static 	int 	syscall_filesize (int);
static 	int 	syscall_read (int, void *, unsigned);
static 	int 	syscall_write (int, const void *, unsigned);
static 	void 	syscall_seek (int, unsigned);
static 	unsigned syscall_tell (int);
static 	void 	syscall_close (int);

typedef int (*syscall_t) (uint32_t, uint32_t, uint32_t);
static syscall_t syscall_function[32];

// static void * esp_;

// File lock
static struct lock fileLock;

struct userFile_t {
	fid_t fid;
	struct list_elem threadElement;
	struct file *f;
};

static bool validateUser (const int *);
static struct userFile_t *fileFromFid (fid_t);
static fid_t allocateFid (void);

void
syscall_init (void) {
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

	lock_init (&fileLock);

	syscall_function[SYS_HALT]     = (syscall_t) syscall_halt;
	syscall_function[SYS_EXIT]     = (syscall_t) syscall_exit;
	syscall_function[SYS_EXEC]     = (syscall_t) syscall_exec;
	syscall_function[SYS_WAIT]     = (syscall_t) syscall_wait;
	syscall_function[SYS_CREATE]   = (syscall_t) syscall_create;
	syscall_function[SYS_REMOVE]   = (syscall_t) syscall_remove;
	syscall_function[SYS_OPEN]     = (syscall_t) syscall_open;
	syscall_function[SYS_FILESIZE] = (syscall_t) syscall_filesize;
	syscall_function[SYS_READ]     = (syscall_t) syscall_read;
	syscall_function[SYS_WRITE]    = (syscall_t) syscall_write;
	syscall_function[SYS_SEEK]     = (syscall_t) syscall_seek;
	syscall_function[SYS_TELL]     = (syscall_t) syscall_tell;
	syscall_function[SYS_CLOSE]    = (syscall_t) syscall_close;

}

static void
syscall_handler (struct intr_frame *f) {
	syscall_t func;
	int *param = f->esp, returnValue;

	if ( !validateUser (param) )
		syscall_exit (-1);

	if (!( validateUser (param + 1) && validateUser (param + 2) && validateUser (param + 3)))
		syscall_exit (-1);

	if (*param < SYS_HALT || *param > SYS_INUMBER)
		syscall_exit (-1);

	func = syscall_function[*param];

	// esp_ = f->esp;
	returnValue = func (*(param + 1), *(param + 2), *(param + 3));
	f->eax = returnValue;
}

static void
syscall_halt (void) {
	const char s[] = "Shutdown";
	const char *p;

	for (p = s; *p != '\0'; p++)
		outb (0x8900, *p);

	asm volatile ("cli; hlt" : : : "memory");
}

static void
syscall_exit (int status) {
	struct thread *th;
	struct list_elem *it;

	th = thread_current ();
	if (lock_held_by_current_thread (&fileLock) )
		lock_release (&fileLock);

	th->ret_status = status;
	printf ("Program ends\n");
	thread_exit ();
}

static pid_t
syscall_exec (const char *cmd_line) {
	lock_acquire (&fileLock);
	int returnValue = process_execute (cmd_line);
	lock_release (&fileLock);
	return returnValue;
}

static int
syscall_wait (pid_t pid) {
	return process_wait (pid);
}

static bool
syscall_create (const char *file, unsigned initial_size) {
	if (file == NULL) 
		syscall_exit (-1);
	
	lock_acquire (&fileLock);
	int returnValue = filesys_create (file, initial_size);
	lock_release (&fileLock);
	return returnValue;
}

static bool
syscall_remove (const char *file) {
	if (file == NULL) 
		syscall_exit (-1);
	
	lock_acquire (&fileLock);
	bool returnValue = filesys_remove (file);
	lock_release (&fileLock);
	return returnValue;
}

static int
syscall_open (const char *file) {
	if (file == NULL) 
		return -1;
	
	struct file *openFile;
	struct userFile_t *userFile;

	lock_acquire (&fileLock);
	openFile = filesys_open (file);
	lock_release (&fileLock);

	if (openFile == NULL) 
		return -1;

	userFile = (struct userFile_t *) malloc (sizeof (struct userFile_t));
	if (userFile == NULL) {
		file_close (openFile);
		return -1;
	}

	lock_acquire (&fileLock);
	list_push_back (&thread_current ()->files, &userFile->threadElement);
	userFile->fid = allocateFid ();
	userFile->f = openFile;
	lock_release (&fileLock);

	return userFile->fid;	
}

static int
syscall_filesize (int fd) {
	struct userFile_t *userFile;
	int size = -1;

	userFile = fileFromFid (fd);
	if (userFile == NULL)
		return -1;

	lock_acquire (&fileLock);
	size = file_length (userFile->f);
	lock_release (&fileLock);

	return size;
}

static int
syscall_read (int fd, void *buffer, unsigned size) {

	int returnValue = -1;
	struct userFile_t *userFile;

	if (!validateUser (buffer) || !validateUser (buffer + size)) {
		syscall_exit (-1);
	} else if (fd == STDIN_FILENO){
		unsigned i = 0;

		lock_acquire (&fileLock);
		for( ; i < size; i++){
			((uint8_t *)buffer)[i] = input_getc();
		}
		lock_release (&fileLock);

		returnValue = size;
	} else if(fd == STDOUT_FILENO) {
		// read from output error
	} else {
		userFile = fileFromFid (fd);
		if (userFile == NULL) 
			syscall_exit (-1);

		lock_acquire (&fileLock);
		returnValue = file_read (userFile->f, buffer, size);
		lock_release (&fileLock);
	}
	return returnValue;
}

static int
syscall_write (int fd, const void *buffer, unsigned size) {
	// const void *esp = (const void*)_esp;

	int returnValue = -1;
	struct userFile_t *userFile;

	if (fd == STDIN_FILENO) {
		returnValue = -1;
	} else if (fd == STDOUT_FILENO) {
		putbuf (buffer, size);
		returnValue = size;
	} else if ( !validateUser (buffer) || !validateUser (buffer + size) ) {
		syscall_exit (-1);
	} else {
		userFile = fileFromFid (fd);
		if (userFile == NULL)
			returnValue = -1;
		else {

			lock_acquire (&fileLock);
			returnValue = file_write (userFile->f, buffer, size);
			lock_release (&fileLock);  
		}
	}
	return returnValue;
}

static void
syscall_seek (int fd, unsigned position) {
	struct userFile_t *userFile;

	userFile = fileFromFid (fd);
	if (userFile == NULL)
		syscall_exit (-1);

	lock_acquire (&fileLock);
	file_seek (userFile->f, position);
	lock_release (&fileLock);
}

static unsigned
syscall_tell (int fd) {
	struct userFile_t *userFile;
	unsigned position;

	userFile = fileFromFid (fd);
	if (userFile == NULL)
		syscall_exit (-1);

	lock_acquire (&fileLock);
	position = file_tell (userFile->f);
	lock_release (&fileLock);

	return position;
}

static void
syscall_close (int fd) {
	struct userFile_t *userFile;

	userFile = fileFromFid (fd);
	if (userFile == NULL)
		syscall_exit (-1);

	lock_acquire (&fileLock);
	list_remove (&userFile->threadElement);
	file_close (userFile->f);
	free (userFile);
	lock_release (&fileLock);
}

static bool
validateUser (const int *address) {
	return true;
}

static struct userFile_t *
fileFromFid (int fid)
{
	struct thread *th;
	struct list_elem *it;

	th = thread_current();
	for (it = list_begin (&th->files); it != list_end (&th->files);
		it = list_next (it))
	{
		struct userFile_t *userFile = list_entry (it, struct userFile_t, threadElement);
		if (userFile->fid == fid)
			return userFile;
	}

	return NULL;
}

static fid_t
allocateFid (void) {
	static fid_t fid = 2;
	return fid++;
}

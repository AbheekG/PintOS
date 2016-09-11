#include <stdio.h>
#include <syscall-nr.h>
#include <inttypes.h>
#include <list.h>

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"

#include "devices/input.h"

#include "filesys/file.h"
#include "filesys/filesys.h"

#include "userprog/syscall.h"
#include "userprog/process.h"

typedef int pid_t;
typedef int fid_t;

static void syscall_handler (struct intr_frame *);

static void syscall_halt (void);
static void syscall_exit (int);
static pid_t syscall_exec (const char *);
static int syscall_wait (pid_t);
static bool syscall_create (const char *, unsigned );
static bool syscall_remove (const char *);
static int syscall_open (const char *);
static int syscall_filesize (int);
static int syscall_read (int, void *, unsigned);
static int syscall_write (int, const void *, unsigned);
static void syscall_seek (int, unsigned);
static unsigned syscall_tell (int);
static void syscall_close (int);

// File lock
static struct lock fileLock;

struct userFile_t {
	fid_t fid;
	struct list_elem threadElement;
	struct file *f;
};

static struct userFile_t *fileFromFid (fid_t);
static fid_t allocateFid (void);
static mapid_t allocate_mapid (void);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
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

}

static pid_t
syscall_exec (const char *cmd_line) {
	lock_acquire (&fileLock);
	int returnValue = process_execute (cmd_line);
	lock_release (&fileLock);
	return returnValue;
}

static void
syscall_wait (pid_t pid) {
	process_wait (pid);
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
syscall_remove (const char *file, unsigned initial_size) {
	if (file == NULL) 
		syscall_exit (-1);
	
	lock_acquire (&fileLock);
	int returnValue = filesys_remove (file, initial_size);
	lock_release (&fileLock);
	return returnValue;
}

static int
syscall_open (const char *file) {
	if (file == NULL) 
		return -1;
	
	struct file *openFile;
	struce userFile_t *userFile;

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
	userFile->fid = allocate_fid ();
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
	size = file_length (userFile);
	lock_release (&fileLock);

	return size;
}

// int read (int fd, void *buffer, unsigned size) ;
// int write (int fd, const void *buffer, unsigned size) ;

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
	position = file_tell (userFile->f, position);
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

// fid allocation
static fid_t
allocate_fid (void)
{
  static fid_t nextFid = 2;
  return nextFid++;
}

// mapid allocation
static mapid_t
allocate_mapid (void)
{
  static mapid_t nextMapid = 0;
  return nextMapid++;
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

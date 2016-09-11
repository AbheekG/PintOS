#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void halt (void) ;
void exit (int status) ;
pid_t exec (const char *cmd_line) ;
int wait (pid_t pid) ;
bool create (const char *file, unsigned initial_size) ;
bool remove (const char *file) ;
int open (const char *file) ;
int filesize (int fd) ;
int read (int fd, void *buffer, unsigned size) ;
int write (int fd, const void *buffer, unsigned size) ;
void seek (int fd, unsigned position) ;
unsigned tell (int fd) ;
void close (int fd) ;

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

void
halt (void) {
	shutdown_power_off ();
}

void
exit (int status) {

}

pid_t
exec (const char *cmd_line) {
	lock_acquire (&fileLock);
	int returnValue = process_execute (cmd_line);
	lock_release (&fileLock);
	return returnValue;
}

void
wait (pid_t pid) {
	process_wait (pid);
}

bool
create (const char *file, unsigned initial_size) {
	if (file == NULL) 
		exit (-1);
	
	lock_acquire (&fileLock);
	int returnValue = filesys_create (file, initial_size);
	lock_release (&fileLock);
	return returnValue;
}

bool
remove (const char *file, unsigned initial_size) {
	if (file == NULL) 
		exit (-1);
	
	lock_acquire (&fileLock);
	int returnValue = filesys_remove (file, initial_size);
	lock_release (&fileLock);
	return returnValue;
}

int
open (const char *file) {
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

int
filesize (int fd) {
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

void
seek (int fd, unsigned position) {
	struct userFile_t *userFile;

	userFile = fileFromFid (fd);
	if (userFile == NULL)
		exit (-1);

	lock_acquire (&fileLock);
	file_seek (userFile->f, position);
	lock_release (&fileLock);
}

unsigned
tell (int fd) {
	struct userFile_t *userFile;
	unsigned position;

	userFile = fileFromFid (fd);
	if (userFile == NULL)
		exit (-1);

	lock_acquire (&fileLock);
	position = file_tell (userFile->f, position);
	lock_release (&fileLock);

	return position;
}

void
close (int fd) {
	struct userFile_t *userFile;

	userFile = fileFromFid (fd);
	if (userFile == NULL)
		exit (-1);

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

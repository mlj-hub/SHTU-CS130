#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static bool check_ptr(void * ptr);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void * esp = f->esp;
  int sys_num = *(int *)esp;
  void * argv[3]={esp+4, esp+8,esp+12};
  // choose which syscall to call according to sys_num
  switch(sys_num){
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      exit(*(int *)argv[0]);
      break;
    case SYS_EXEC:
      f->eax = exec(*(const char **)argv[0]);
      break;
    case SYS_WAIT:
      f->eax = wait(*(pid_t*)argv[0]);
      break;
    case SYS_REMOVE:
      f->eax = remove(*(const char **)argv[0]);
      break;
    case SYS_OPEN:
      f->eax = open(*(const char **)argv[0]);
      break;
    case SYS_FILESIZE:
      f->eax = filesize(*(int *)argv[0]);
      break;
    case SYS_TELL:
      f->eax = tell(*(int *)argv[0]);
      break;
    case SYS_CLOSE:
      close(*(int *)argv[0]);
      break;
    case SYS_SEEK:
      seek(*(int *)argv[0],*(unsigned*)argv[1]);
      break;
    case SYS_CREATE:
      f->eax = create(*(const char **)argv[0],*(unsigned*)argv[1]);
      break;
    case SYS_READ:
      f->eax = read(*(int*)argv[0],*(void **)argv[1],*(unsigned*)argv[2]);
    case SYS_WRITE:
      f->eax = write(*(int*)argv[0],*(const void **)argv[1],*(unsigned*)argv[2]);
      break;
    default:
      printf("invalid sys call number! exit\n");
      NOT_REACHED();
  }

  printf ("system call!\n");
}


/* Check whether the given pointer is valid */
static bool check_ptr(void * ptr)
{
  struct thread * t = thread_current();
  if(!is_user_vaddr(ptr) || !ptr || !pagedir_get_page(t->pagedir,ptr))
    return false;  
  return true;
}

void halt (void) 
{

}

void exit (int status)
{

}

pid_t exec (const char *file)
{

}

int wait (pid_t pid)
{

}

bool create (const char *file, unsigned initial_size){}
bool remove (const char *file){}
int open (const char *file){}
int filesize (int fd){}
int read (int fd, void *buffer, unsigned length){}
int write (int fd, const void *buffer, unsigned length){}
void seek (int fd, unsigned position){}
unsigned tell (int fd){}
void close (int fd){}
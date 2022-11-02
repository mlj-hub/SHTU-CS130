#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
static bool check_ptr(const void * ptr);
static bool check_esp(const void * esp_);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  void * esp = f->esp;
  // check the address space of whole arguments
  if(!check_esp(esp))
  {
    exit(-1);
  }
  int sys_num = *(int *)esp;
  void * argv[3]={esp+4, esp+8,esp+12};
  // choose which syscall to call according to sys_num
  switch(sys_num)
  {
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
      break;
    case SYS_WRITE:
      f->eax = write(*(int*)argv[0],*(const void **)argv[1],*(unsigned*)argv[2]);
      break;
    default:
      exit(-1);
      NOT_REACHED();
  }
}


/* Check whether the given pointer is valid */
static bool
check_ptr(const void * ptr)
{
  struct thread * t = thread_current();
  if(!is_user_vaddr(ptr) || !ptr || !pagedir_get_page(t->pagedir,ptr))
    return false;  
  return true;
}

static bool
check_esp(const void * esp)
{
  // check the whole address space of sysnum
  if(!check_ptr(esp)||!check_ptr(esp+3))
    return false;
  int sys_num = *(int *)esp;
  esp +=sizeof(int);
  bool success = true;
  // check the whole address space of arguments
  switch(sys_num)
  {
    case SYS_HALT:
      break;
    case SYS_EXIT:
    case SYS_EXEC:
    case SYS_WAIT:
    case SYS_REMOVE:
    case SYS_OPEN:
    case SYS_FILESIZE:
    case SYS_TELL:
    case SYS_CLOSE:
      if(!check_ptr(esp) || !check_ptr(esp+3))
        success = false;
      break;
    case SYS_SEEK:
    case SYS_CREATE:
      if(!check_ptr(esp) || !check_ptr(esp+7))
        success = false;
      break;
    case SYS_READ:
    case SYS_WRITE:
      if(!check_ptr(esp) || !check_ptr(esp+7))
        success = false;
      break;
    default:
      success = false;
  }
  return success;
}

void halt (void) 
{

}

void
exit (int status)
{
  struct thread * t= thread_current();
  t->child_info->exit_status = status;
  thread_exit();
  NOT_REACHED();
}

pid_t exec (const char *file)
{
  if(!check_ptr((const void *)file))
    return -1;
  int pid;
  pid = process_execute(file);
  
}

int wait (pid_t pid)
{
  return process_wait(pid);
}

bool create (const char *file, unsigned initial_size){}
bool remove (const char *file){}
int open (const char *file){}
int filesize (int fd){}
int read (int fd, void *buffer, unsigned length){}

int write (int fd, const void *buffer, unsigned length)
{
  for(void * i = buffer;i<buffer+length;i+=PGSIZE)
    check_ptr(i);
  if(fd == 1)
  {
    putbuf((const char *)buffer,length);
    return length;
  }
  else
  {
    return 0;
  }
}

void seek (int fd, unsigned position){}
unsigned tell (int fd){}
void close (int fd){}
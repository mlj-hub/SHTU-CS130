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
static struct file * get_file(int fd);

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
  shutdown_power_off();
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

/* Create a file with initial_size. Return true if success and false otherwise */
bool create (const char *file, unsigned initial_size)
{
  if(!check_ptr(file))
    return false;
  thread_acquire_file_lock();
  bool success =  filesys_create (file,initial_size);
  thread_release_file_lock();
  return success;
}

/* Remove a file. Return true if success and false otherwise */
bool remove (const char *file)
{
   if(!check_ptr(file))
    return false;
  thread_acquire_file_lock();
  bool success =  filesys_remove (file);
  thread_release_file_lock();
  return success;
}

int open (const char *file)
{
  if(!check_ptr(file))
    return -1;
  thread_acquire_file_lock();
  struct file * temp = filesys_open(file);
  thread_release_file_lock();
  if(temp){
    int a =thread_add_file(temp);
    return a;
  }
  else
    return -1;
}

/* Return file size according to the file descriptior*/
int filesize (int fd)
{
  thread_acquire_file_lock();
  int size = file_length(get_file(fd));
  thread_release_file_lock();
  return size;
}

int read (int fd, void *buffer, unsigned length)
{
  for(void * i = buffer;i<buffer+length;i+=PGSIZE)
    check_ptr(i);
  thread_acquire_file_lock();
  int size = 0;
  if(fd == 0)
  {
    size = input_getc();
  }
  else
  {
    size = file_read(get_file(fd),buffer,length);
  }
  thread_release_file_lock();  
  return size;
}

int write (int fd, const void *buffer, unsigned length)
{
  for(void * i = buffer;i<buffer+length;i+=PGSIZE)
  {
    if(!check_ptr(i))
      return -1;
  } 
  if(fd == 1)
  {
    putbuf((const char *)buffer,length);
    return length;
  }
  else
  {
    struct file* file = get_file(fd);
    thread_acquire_file_lock();
    int size = file_write(file,buffer,length);
    thread_release_file_lock();
    return size;
  }
}

void seek (int fd, unsigned position)
{
  struct file * file =  get_file(fd);
  thread_acquire_file_lock();
  file_seek(file,position);
  thread_release_file_lock();
}

unsigned tell (int fd)
{
  struct file * file =  get_file(fd);
  thread_acquire_file_lock();
  unsigned pos = file_tell(file);
  thread_release_file_lock();
  return pos;
}

void close (int fd)
{
   struct file * file =  get_file(fd);
  thread_acquire_file_lock();
  file_close(file);
  thread_release_file_lock();
}

/* Get the file according to the file descriptor */
static struct file *
get_file(int fd)
{
  struct thread * t = thread_current();
  // find the file according to the file descriptor
  for(struct list_elem* i =list_begin(&t->owned_files);i!=list_end(&t->owned_files);i=list_next(i))
  {
    struct thread_file * temp = list_entry(i,struct thread_file,file_elem);
    if(temp->fd == fd)
    {
      return temp->file;
    }
  }
  // if there is no such file, return NULL
  return NULL;
}
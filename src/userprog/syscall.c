#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "threads/malloc.h"
#include "vm/mmap.h"
#include "vm/frame.h"

static void syscall_handler (struct intr_frame *);
static bool check_ptr(const void * ptr);
static bool check_esp(const void * esp_);
static struct thread_file * get_thread_file(int fd);
static struct file * get_file(int fd);
static bool exsit_overlap_mmap(uint32_t addr,uint32_t size);
static void free_mmap(struct mmap_entry * mmp);
static bool vm_check_buffer(void * buffer, uint32_t size);

static void * sys_esp;

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
  sys_esp = f->esp;
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
    case SYS_MMAP:
      f->eax = mmap(*(int*)argv[0],*(void**)argv[1]);
      break;
    case SYS_MUNMAP:
      munmap(*(mapid_t*)argv[0]);
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

/* Check the validation of the given esp and the address of the arguments  */
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
    case SYS_MUNMAP:
      if(!check_ptr(esp) || !check_ptr(esp+3))
        success = false;
      break;
    case SYS_SEEK:
    case SYS_CREATE:
    case SYS_MMAP:
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

/* Check the validation of all character in a string */
static bool
check_str(const char * str)
{
  for (const char *i = str;;i++){
    if(!check_ptr(i))
      return false;
    if(*i=='\0')
      return 1;
  }
}

/* SysCall halt */
void halt (void) 
{
  shutdown_power_off();
}

/* SysCall exit */
void
exit (int status)
{
  struct thread * t= thread_current();
  /* Update the information of exit status */
  t->child_info->exit_status = status;
  thread_exit();
  NOT_REACHED();
}

/* SysCall exit */
pid_t exec (const char *file)
{
  if(!check_str(file))
    exit(-1);
  int pid;
  pid = process_execute(file);
  return pid;
}

/* SysCall wait */
int wait (pid_t pid)
{
  return process_wait(pid);
}

/* Create a file with initial_size. Return true if success and false otherwise */
bool create (const char *file, unsigned initial_size)
{
  /* If the string FILE is not valid, exit(-1) */
  if(!check_str(file))
    exit(-1);
  thread_acquire_file_lock();
  bool success =  filesys_create (file,initial_size);
  thread_release_file_lock();
  return success;
}

/* Remove a file. Return true if success and false otherwise */
bool remove (const char *file)
{
  /* If the string FILE is not valid, exit(-1) */
  if(!check_str(file))
    exit(-1);
  thread_acquire_file_lock();
  bool success =  filesys_remove (file);
  thread_release_file_lock();
  return success;
}

int open (const char *file)
{
  if(!check_str(file))
    exit(-1);
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

/* SysCall read */
int read (int fd, void *buffer, unsigned length)
{
  // Check the validation of buffer
  if(!vm_check_buffer(buffer,length))
    exit(-1);

  // Read from the STDIN
  if(fd == 0)
  {
    return input_getc();
  }
  struct file* file = get_file(fd);
  // If no such file, exit
  if(!file)
    exit(-1);
  thread_acquire_file_lock();
  int size = 0;
  size = file_read(file,buffer,length);
  thread_release_file_lock();  
  return size;
}

/* SysCall write */
int write (int fd, const void *buffer, unsigned length)
{
  // Check the validation of buffer
  if(!check_ptr(buffer)||!check_ptr(buffer+length-1))
    exit(-1);
  // write to the STDOUT
  if(fd == 1)
  {
    putbuf((const char *)buffer,length);
    return length;
  }
  else
  {
    struct file* file = get_file(fd);
    if(!file)
      exit(-1);
    thread_acquire_file_lock();
    int size = file_write(file,buffer,length);
    thread_release_file_lock();
    return size;
  }
}

/* SysCall seek */
void seek (int fd, unsigned position)
{
  struct file * file =  get_file(fd);
  if(!file)
    exit(-1);
  thread_acquire_file_lock();
  file_seek(file,position);
  thread_release_file_lock();
}

/* SysCall tell */
unsigned tell (int fd)
{
  struct file * file =  get_file(fd);
  thread_acquire_file_lock();
  unsigned pos = file_tell(file);
  thread_release_file_lock();
  return pos;
}

/* SysCall close */
void close (int fd)
{
  struct thread_file * thread_file =  get_thread_file(fd);
  if(!thread_file)
    exit(-1);
  if(thread_file->opened == 0)
    exit(-1);
  thread_acquire_file_lock();
  file_close(thread_file->file);
  thread_close_file(fd);
  thread_release_file_lock();
}

/* SysCall mmap */
mapid_t mmap (int fd, void *addr_){
  uint32_t addr = (uint32_t)addr_;
  // cannot map input and output
  if(fd == 0 || fd == 1)
    return -1;
  // cannot mapt 0 or unaligned addr
  if(addr == 0 || (uint32_t)addr%PGSIZE !=0)
    return -1;
  
  thread_acquire_file_lock();
  struct thread_file* thread_file = get_thread_file(fd);
  thread_release_file_lock();
  if(!thread_file)
    return -1;
  if(!thread_file->file)
    return -1;

  int32_t file_size = file_length(thread_file->file);
  if(file_size == 0)
    return -1;
  if(exsit_overlap_mmap((uint32_t)addr,file_size))
    return -1;

  thread_acquire_file_lock();
  struct file * f = file_reopen(thread_file->file);
  thread_release_file_lock();

  // create supplement page entries for the mapped file
  uint32_t stick_out_page = pg_round_down(addr+file_size);
  struct thread * cur = thread_current();

  struct mmap_entry * mmap = malloc(sizeof(struct mmap_entry));
  if(!mmap)
    return -1;
  mmap->page_num = 0;
  int success = 1;
  uint32_t page_num = 0;
  if(file_size % PGSIZE ==0)
    page_num = file_size/PGSIZE;
  else
    page_num = file_size/PGSIZE+1;
  // get supplement page entry for each page within the file
  for(uint32_t ofs = 0;ofs<page_num;ofs+=1)
  {
    uint32_t page_start = ofs*PGSIZE+addr;
    struct supl_page_entry * temp = malloc(sizeof(struct supl_page_entry));
    // fail to get a supplement page entry
    if(!temp)
    {
      success = -1;
      break;
    }

    temp->writable = true;
    temp->uaddr = addr+ofs*PGSIZE;
    temp->type = Type_MMP;
    temp->file = f;
    temp->file_ofs = ofs*PGSIZE;
    temp->resident = false;
    if(page_start == stick_out_page)
      temp->file_size = file_size-ofs*PGSIZE;
    else
      temp->file_size = PGSIZE;
    lock_init(&temp->supl_lock);
    list_push_back(&cur->supl_page_table,&temp->elem);

    mmap->page_num++;
  }

  if(success == -1)
  {
    free(mmap);
    return -1;
  }

  mmap->file = f;
  mmap->start_uaddr = addr;
  mmap->mapid = cur->next_mapid;
  list_push_back(&cur->mapped_list,&mmap->elem);

  return cur->next_mapid++;
}

void 
munmap(mapid_t mapid)
{
  struct thread* cur = thread_current();
  struct mmap_entry * mmp=NULL;
  // find the file in the mapped list of this thread
  for(struct list_elem * i = list_begin(&cur->mapped_list);i!=list_end(&cur->mapped_list);i=list_next(i))
  {
    struct mmap_entry * temp = list_entry(i,struct mmap_entry,elem);
    if(temp->mapid==mapid)
    {
      mmp = temp;
      break;
    }
  }
  // the file is not mapped currently
  if(!mmp)
    return;

  free_mmap(mmp);
  list_remove(&mmp->elem);
  free(mmp);
}

/* Get the thread_file according to the file descriptor */
static struct thread_file *
get_thread_file(int fd)
{
  struct thread * t = thread_current();
  // find the file according to the file descriptor
  for(struct list_elem* i =list_begin(&t->owned_files);i!=list_end(&t->owned_files);i=list_next(i))
  {
    struct thread_file * temp = list_entry(i,struct thread_file,file_elem);
    if(temp->fd == fd)
    {
      return temp;
    }
  }
  // if there is no such file, return NULL
  return NULL;
}

/* Get the file according ot the given file descriptor */
static struct file *
get_file(int fd)
{
  struct thread_file * thread_file = get_thread_file(fd);
  if(thread_file)
    return thread_file->file;
  return NULL;
}

/* Check whether the memory starts form ADDR and has size SIZE overlaps with 
   any existed file map, executable or stack. */
static bool
exsit_overlap_mmap(uint32_t addr_,uint32_t size)
{
  struct thread * cur = thread_current();
  uint32_t addr = pg_round_down(addr_);
  uint32_t page_num = 0;
  if(size % PGSIZE ==0){
    page_num = size/PGSIZE;
  }
  else
    page_num = size/PGSIZE+1;
  for(uint32_t oft=0;oft<page_num;oft+=1)
  {
    for(struct list_elem * e = list_begin(&cur->supl_page_table);e!=list_end(&cur->supl_page_table);e = list_next(e))
    {
      struct supl_page_entry * temp = list_entry(e,struct supl_page_entry,elem);
      if(temp->uaddr == addr+oft*PGSIZE)
        return true;
    }
  }
  return false;
}

/* Free all pages and supplement page entry which is setted for it */
static void
free_mmap(struct mmap_entry * mmp)
{
  struct thread * cur = thread_current();
  // the list of all supplement pages of current thread
  struct list * s_p_table = &cur->supl_page_table;
  for(int i = 0;i<mmp->page_num;i++)
  {
    bool finded = false;
    for(struct list_elem * e = list_begin(s_p_table);e!=list_end(s_p_table);e = list_next(e))
    {
      struct supl_page_entry * temp = list_entry(e, struct supl_page_entry,elem);
      // if the page is in the memory, free it
      if(temp->uaddr == mmp->start_uaddr+i*PGSIZE)
      {
        finded = true;
        lock_acquire(&temp->supl_lock);
      }
      if(temp->uaddr == mmp->start_uaddr+i*PGSIZE && temp->resident)
      {
        // if dirty, write it back to the corresponding file
        if(pagedir_is_dirty(cur->pagedir,temp->uaddr))
        {
          thread_acquire_file_lock();
          file_write_at(mmp->file,temp->uaddr,temp->file_size,temp->file_ofs);
          thread_release_file_lock();
        }
        // free the kernel address according to the supplemental page entry
        if(temp->resident)
        {
         frame_free(temp->kaddr);
          // remove the corresponding user virtual address from the page directory
          pagedir_clear_page(cur->pagedir,temp->uaddr);
        }
      }
      if(finded)
      {
        list_remove(e);
        lock_release(&temp->supl_lock);
        free(temp);
        break;
      }
    }
    if(finded == false)
    {
      PANIC("cannot find a supplemental page table entry when unmap a file\n");
    }
  }
}

static bool
vm_check_ptr(void * ptr)
{
  if(!ptr || !is_user_vaddr(ptr))
    return false;
  bool success = false;
  struct thread * cur = thread_current();
  if(!pagedir_get_page(cur->pagedir,ptr))
  {
    struct list * supl_page_table = &thread_current()->supl_page_table;
    struct supl_page_entry * fault_page=NULL;
    uint32_t fault_addr = ptr;
    for(struct list_elem * i=list_begin(supl_page_table);i!=list_end(supl_page_table);i=list_next(i))
    {
      fault_addr = pg_round_down(ptr);
      struct supl_page_entry * temp = list_entry(i,struct supl_page_entry,elem);
      if(temp->uaddr == fault_addr)
      {
        fault_page = temp;
        break;
      }
    }

    if(fault_page != NULL)
    {
      success = load_page(fault_page);
    }
    else // grow stack
    {
      if(fault_addr>=sys_esp-32  && fault_addr>= PHYS_BASE - STACK_LIMIT)
        success = grow_stack(fault_addr);
    }
    return success;
  }
  else
    return true;
}

static bool
vm_check_buffer(void * buffer, uint32_t size)
{
  uint32_t page_start = pg_round_down((uint32_t)buffer);
  uint32_t page_end = pg_round_down((uint32_t)buffer+size);
  for(uint32_t i = page_start;i<=page_end;i+=PGSIZE)
  {
    if(!vm_check_ptr(i))
      return false;
  }
  return true;
}
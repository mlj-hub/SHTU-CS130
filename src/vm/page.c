#include "vm/page.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "lib/string.h"
#include "userprog/process.h"
#include "vm/swap.h"
#include "devices/timer.h"

/* Load a page into memory according to the supplemental page entry */
bool 
load_page(struct supl_page_entry * t)
{
  /* get a page from the memory */
  uint32_t kaddr = frame_allocate(PAL_USER,t);
  if(!kaddr)
    return false;
  lock_acquire(&t->supl_lock);
  bool success = true;
  switch(t->type)
  {
    case Type_MMP:
      thread_acquire_file_lock();
      file_read_at(t->file,(void *)kaddr, t->file_size, t->file_ofs);
      thread_release_file_lock();
      if(t->file_size != PGSIZE)
        memset(kaddr+t->file_size,0,PGSIZE-t->file_size);     
      break;
    case Type_Exe:
      thread_acquire_file_lock();
      file_read_at(t->file,(void *)kaddr, t->file_size, t->file_ofs);
      thread_release_file_lock();
      if(t->file_size != PGSIZE)
        memset(kaddr+t->file_size,0,PGSIZE-t->file_size);   
      break;
    case Type_Swap:
      read_from_swap(t->swap_ofs, kaddr);
      break;
    default:
      PANIC("Unkonwn page type");
      break;
  }
  if(!install_page(t->uaddr,kaddr,t->writable))
    success = false;
  if(success)
  {
    t->resident = true;
    t->kaddr = kaddr;
    t->last_accessed_time = timer_ticks();
  }
  lock_release(&t->supl_lock);
  return success;
}

bool
grow_stack(void * fault_addr)
{
  struct supl_page_entry * new_page_entry = malloc(sizeof(struct supl_page_entry));

  if (new_page_entry == NULL)
    return false;

  uint32_t  new_frame = frame_allocate(PAL_USER,new_page_entry);

	if (new_frame == NULL) {
		free(new_page_entry);
		return false;
	}

  /*set up new entry!!!*/
  new_page_entry->type = Type_Swap;
  new_page_entry->kaddr = new_frame;
  new_page_entry->uaddr = pg_round_down(fault_addr);
  new_page_entry->writable = true;
  new_page_entry->resident = true;
  new_page_entry->last_accessed_time = timer_ticks();
  lock_init(&new_page_entry->supl_lock);

    /*install_page!!!*/
	if (!install_page(new_page_entry->uaddr, new_frame,
        new_page_entry->writable)) 
	{
		free(new_page_entry);
		frame_free(new_frame);
		return false;
	}


    /* add new entry to s_page_table */
	struct list * supl_page_table = &thread_current()->supl_page_table;

	list_push_back(supl_page_table, &new_page_entry->elem);

	return true;

}
#include "vm/page.h"

#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "lib/string.h"
#include "userprog/process.h"

/* Load a page into memory according to the supplemental page entry */
bool 
load_page(struct supl_page_entry * t)
{
  /* get a page from the memory */
  uint32_t kaddr = frame_allocate(PAL_USER);
  if(!kaddr)
    return false;
  bool success = true;
  switch(t->type)
  {
    case Type_MMP:
      thread_acquire_file_lock();
      file_read_at(t->file,(void *)kaddr, t->file_size, t->file_ofs);
      thread_release_file_lock();
      t->kaddr = kaddr;
      t->resident = true;
      if(t->file_size != PGSIZE)
        memset(t->kaddr+t->file_size,0,PGSIZE-t->file_size);     
      if(!install_page(t->uaddr,kaddr,true))
        success = false;
      break;
    case Type_Swap:
      break;
    default:
      PANIC("Unkonwn page type");
      break;
  }
  return success;
}

bool
grow_stack(void * fault_addr)
{
  struct supl_page_entry * new_page_entry = malloc(sizeof(struct supl_page_entry));

  if (new_page_entry == NULL)
    return false;

  uint32_t * new_frame = frame_allocate(PAL_USER);

	if (new_frame == NULL) {
		free(new_page_entry);
		return false;
	}

  /*set up new entry!!!*/
  new_page_entry->type = 4;
  new_page_entry->kaddr = new_frame;
  new_page_entry->uaddr = fault_addr;
  new_page_entry->writable = true;
  new_page_entry->resident = true;

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
	struct lock * supl_page_table_lock = &thread_current()->supl_page_table_lock;

	if (!lock_held_by_current_thread(&thread_current()->supl_page_table_lock))
  {
		lock_acquire(&thread_current()->supl_page_table_lock);
	}
    
	list_push_back(supl_page_table, &new_page_entry->elem);
	lock_release(supl_page_table_lock);

	return true;

}
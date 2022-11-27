#include "vm/page.h"

#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/frame.h"

bool 
load_page(struct supl_page_entry * t)
{
    /*load_page_in_supl_page_entry*/
    if(t->type == 1)
    {

    }

    /*load_page_in_swap*/
    if(t->type == 2)
    {

    }

    /*load_page_in_mmap*/
    if(t->type == 3)
    {

    }

    return true;
}

bool
grow_stack(void * t,void * fault_addr)
{
    if(t == NULL){return false;}

    struct supl_page_entry * new_page_entry = malloc(sizeof(t));

    if (new_page_entry == NULL) {
		return false;
	}

    uint8_t * new_frame = frame_allocate(PAL_USER);

	if (new_frame == NULL) {
		free(new_page_entry);
		return false;
	}

    /*set up new entry!!!*/
    new_page_entry->owner = thread_current();
    new_page_entry->type = 4;
    new_page_entry->kaddr = new_frame;
    new_page_entry->uaddr = fault_addr;
    new_page_entry->writable = true;

    /*install_page!!!*/
	if (!install_page(new_page_entry->uaddr, new_frame,
        new_page_entry->writable)) 
	{
		free(new_page_entry);
		free(new_frame);
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
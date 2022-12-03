#include "vm/frame.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "lib/string.h"
#include "threads/vaddr.h"
#include "vm/swap.h"

static struct list frame_table;
static struct lock frame_lock;

static void *evict_frame(struct supl_page_entry * );
static struct frame * find_victim_frame(void);

void
frame_init(void)
{
  list_init(&frame_table);
  lock_init(&frame_lock);
}

/* Get a frame from the pool specified by flag and put it into 
   frame table. Return the kernel virtual memory of a frame 
   which can be used. If no pages can be allocated, 
   choose a frame to evict. If swap is full, PANIC the kernel */
void *
frame_allocate(enum palloc_flags flag,struct supl_page_entry * supl_page)
{
  // kernel virtual address allocated from the user pool
  void * vaddr = palloc_get_page(flag);
  if(vaddr==NULL)
  {
    // no more memory, need to evict
    vaddr = evict_frame(supl_page);
    if(!vaddr)
      return NULL;
    else
      return vaddr;
  }
  
  lock_acquire(&frame_lock);
  struct frame * temp = malloc(sizeof (struct frame));
  if(!temp)
  {
    lock_release(&frame_lock);
    return NULL;
  }
  temp->owner = thread_current();
  temp->vaddr = vaddr;
  temp->supl_page = supl_page;
  list_push_back(&frame_table,&temp->elem);
  lock_release(&frame_lock);
  return vaddr;
}

/* Free the page indicated by the kernel virtual address and
   remove the corresponding frame table entry. */
void
frame_free(void * vaddr)
{
  lock_acquire(&frame_lock);
  for(struct list_elem *i=list_begin(&frame_table);i!=list_end(&frame_table);i=list_next(i))
  {
      struct frame * temp = list_entry(i,struct frame,elem);
      if(temp->vaddr == (uint32_t)vaddr)
      {
        list_remove(&temp->elem);
        free(temp);
        break;
      }
  }
  palloc_free_page(vaddr);
  lock_release(&frame_lock);
}

/* Evict a frame from memory and return the corresponding kernel virtual memory */
static void *
evict_frame(struct supl_page_entry * supl_page)
{
  lock_acquire(&frame_lock);
  struct frame * victim_frame = find_victim_frame();
  struct supl_page_entry* s_p_entry = victim_frame->supl_page;
  lock_acquire(&s_p_entry->supl_lock);

  if(pagedir_is_dirty(victim_frame->owner->pagedir,s_p_entry->uaddr))
  {
    if(s_p_entry->type == Type_MMP)
    {
      thread_acquire_file_lock();
      file_write_at(s_p_entry->file,s_p_entry->uaddr,s_p_entry->file_size,s_p_entry->file_ofs);
      thread_release_file_lock();
    }
    else
    {
      // write it to swap
      int32_t swap_ofs = write_to_swap(victim_frame->vaddr);
      if(swap_ofs == SWAP_INDEX_ERROR)
      {
        lock_release(&s_p_entry->supl_lock);
        lock_release(&frame_lock);
        return NULL;
      }
      s_p_entry->swap_ofs = swap_ofs;
      s_p_entry->type = Type_Swap;
    }
  }
  // remove the frame from the owner's pagedir
  pagedir_clear_page(victim_frame->owner->pagedir,s_p_entry->uaddr);
  s_p_entry->resident = false;
  lock_release(&s_p_entry->supl_lock);
  victim_frame->owner = thread_current();
  victim_frame->supl_page = supl_page;
  memset((void *)victim_frame->vaddr,0,PGSIZE);

  lock_release(&frame_lock);
  return victim_frame->vaddr;
}

/* Find the free frame and return the frame table entry */
static struct frame *
find_victim_frame(void)
{
  struct frame * victim = NULL;
  for(struct list_elem *i=list_begin(&frame_table);i!=list_end(&frame_table);i=list_next(i))
  {
    struct frame * temp = list_entry(i,struct frame,elem);
    if(!pagedir_is_accessed(temp->owner->pagedir,temp->supl_page->uaddr))
    {
      victim = temp;
      break;
    }
  }

  if(victim != NULL)
    return victim;

  int32_t oldest_page_time = INT32_MAX;
  for(struct list_elem *i=list_begin(&frame_table);i!=list_end(&frame_table);i=list_next(i))
  {
    struct frame * temp = list_entry(i,struct frame,elem);
    if(temp->supl_page->last_accessed_time<oldest_page_time)
    {
      oldest_page_time = temp->supl_page->last_accessed_time;
      victim = temp;
    }
  }
  return victim;
}

void 
free_process_page(struct thread * t)
{

  lock_acquire(&frame_lock);

  for(struct list_elem * i = list_begin(&frame_table);i!=list_end(&frame_table);)
  {
    struct frame * temp = list_entry(i,struct frame,elem);
    if(temp->owner == t)
    {
      i = list_remove(i);
      free(temp);
    }
    else
      i = list_next(i);
  }
  lock_release(&frame_lock);
}
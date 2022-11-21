#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "lib/string.h"
#include "threads/vaddr.h"

static struct list frame_table;
static struct lock frame_lock;

static void * evict_frame();
static struct frame * find_free_frame();

void
frame_init()
{
    list_init(&frame_table);
    lock_init(&frame_lock);
}

/* Get a frame from the pool specified by flag and put it into 
   frame table. Return the kernel virtual memory of a frame 
   which can be used. If no pages can be allocated, 
   choose a frame to evict. If swap is full, PANIC the kernel */
void *
frame_allocate(enum palloc_flags flag)
{
    void * vaddr = palloc_get_page(flag);
    if(vaddr==NULL)
    {
        return evict_frame();
    }
    else
    {
        lock_acquire(&frame_lock);
        struct frame * temp = malloc(sizeof (struct frame)); 
        temp->owner = thread_current();
        temp->vaddr = vaddr;
        temp->free = 0;
        list_push_back(&frame_table,&temp->elem);
        lock_release(&frame_lock);
        return vaddr;
    }    
}

/* Free the page indicated by the kernel virtual address and
   remove the corresponding frame table entry. */
void
frame_free(void * vaddr)
{
    lock_acquire(&frame_table);
    for(struct list_elem *i=list_begin(&frame_table);i!=list_end(&frame_table);i=list_next(i))
    {
        struct frame * temp = list_entry(i,struct frame,elem);
        if(temp->vaddr == vaddr)
        {
            list_remove(temp);
            free(temp);
        }
    }
    palloc_free_page(vaddr);
    lock_release(&frame_table);
}

/* Evict a frame from memory and return the corresponding kernel virtual memory */
static void *
evict_frame()
{
    lock_acquire(&frame_lock);
    struct frame * victim_frame = find_free_frame();
    if(victim_frame != NULL)
    {
        struct thread * old_owner = victim_frame->owner;
        victim_frame->owner = thread_current();
        memset(victim_frame->vaddr,0,PGSIZE);
        // TODO: need to remove the vaddr from the owner's page table
        // pagedir_clear_page(old_owner->pagedir,victim_frame->vaddr);
    }
    lock_release(&frame_lock);
    return victim_frame->vaddr;
}

/* Find the free frame and return the frame table entry */
static struct frame *
find_free_frame()
{
    struct frame * victim = NULL;
    for(struct list_elem *i=list_begin(&frame_table);i!=list_end(&frame_table);i=list_next(i))
    {
        struct frame * temp = list_entry(i,struct frame,elem);
        if(!pagedir_is_accessed(temp->owner->pagedir,temp->vaddr))
        {
            victim = temp;
            break;
        }
    }
    return victim;
}
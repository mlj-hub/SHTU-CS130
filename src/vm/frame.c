#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"

static struct list frame_table;
static struct lock frame_lock;

void
frame_init()
{
    list_init(&frame_table);
    lock_init(&frame_lock);
}

void
frame_allocate()
{
    void * vaddr = palloc_get_page(PAL_USER);
    if(vaddr==NULL)
        PANIC("no free frames");
    else
    {
        lock_acquire(&frame_lock);
        struct frame * temp = malloc(sizeof (struct frame)); 
        temp->owner = thread_current();
        temp->vaddr = vaddr;
        temp->free = 0;
        list_push_back(&frame_table,&temp->elem);
        lock_release(&frame_lock);
    }    
}
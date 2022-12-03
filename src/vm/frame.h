#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "threads/palloc.h"
#include <stdint.h>
#include "vm/page.h"

struct frame
 {
    /* The thread which owns the frame */
    struct thread * owner;
    /* Corresponding kernel virtual address */
    uint32_t vaddr;
    /* List elem for Frame Table */
    struct list_elem elem;
    /* Supl page entry for which refer to this page */
    struct supl_page_entry * supl_page;
 };

void frame_init(void);
void * frame_allocate(enum palloc_flags flag,struct supl_page_entry * supl_page);
void frame_free(void * vaddr);
void free_process_page(struct thread * t);

#endif
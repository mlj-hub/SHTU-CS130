#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "threads/palloc.h"
#include <stdint.h>

struct frame
 {
    /* The thread which owns the frame */
    struct thread * owner;
    /* Corresponding kernel virtual address */
    uint32_t vaddr;
    /* List elem for Frame Table */
    struct list_elem elem;
    bool free;
 };

void frame_init(void);
void * frame_allocate(enum palloc_flags flag);
void frame_free(void * vaddr);

#endif
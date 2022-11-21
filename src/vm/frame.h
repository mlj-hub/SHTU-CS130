#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
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

#endif
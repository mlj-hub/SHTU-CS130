#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include <stdint.h>

struct frame
 {
    struct thread * owner;
    uint32_t vaddr;
    struct list_elem elem;
    bool free;
 };

#endif
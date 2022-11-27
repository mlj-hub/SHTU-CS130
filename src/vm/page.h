#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h"
#include <stdint.h>

struct supl_page_entry
 {
    struct list_elem elem;
    uint32_t uaddr;
    uint32_t kaddr;

    /* use type to determine the type of page fault
       CODE_type=1; SWAP_type=2; MMAP_type=3;
       STACK_type=4; FILE_type=5. */
    uint8_t type;
    struct thread * owner;
    bool writable;
 };


bool load_page(struct supl_page_entry * t);

bool grow_stack(void * t,void * fault_addr);

#endif
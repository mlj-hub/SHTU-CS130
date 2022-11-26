#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h"
#include <stdint.h>

struct supl_page_entry
 {
    struct list_elem elem;
    uint32_t uaddr;
    uint32_t kaddr;
 };

#endif
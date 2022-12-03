#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h"
#include <stdint.h>

#define Type_Swap  1 // in swap
#define Type_MMP   2 // mmaped files
#define Type_Exe   3 // exectutable, which has not been loaded. If loaded and fault, in swap

#define STACK_LIMIT 0x800000

struct supl_page_entry
 {
    struct list_elem elem;
    /* User virtual address to this page */
    uint32_t uaddr;
    /* The corresponding kernel virtual address */
    uint32_t kaddr;

    /* Type of this page */
    int type;
    /* If the page is writable */
    bool writable;
    /* If the page is in the memory or not */
    bool resident;

    /* File which map to this page */
    struct file * file;
    /* File offset for the file */
    int file_ofs;
    /* Size of file in this page */
    int file_size;

    /* Swap offset */
    int swap_ofs;

    /* Last accessed time */
    int32_t last_accessed_time; 
    /* Lock for supl page entry */
    struct lock supl_lock;
 };


bool load_page(struct supl_page_entry * t);

bool grow_stack(void * fault_addr);

#endif
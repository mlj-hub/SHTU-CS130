#ifndef SWAP_H
#define SWAP_H
#include "devices/block.h"
#include "lib/kernel/bitmap.h"

#define SWAP_INDEX_ERROR -1


void swap_init(void);
int32_t write_to_swap(uint32_t kaddr);
bool read_from_swap(uint32_t idx,uint32_t kaddr);

#endif
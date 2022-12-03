#include "swap.h"
#include "threads/vaddr.h"
#include "threads/thread.h"

static struct block * swap_device;
static struct bitmap * swap_bit_map;
static struct lock swap_lock;
const int sector_per_page = PGSIZE/BLOCK_SECTOR_SIZE;
const bool allocated = true;
const bool not_allocated = false;

static bool check_idx(uint32_t idx);

void
swap_init(void)
{
  swap_device = block_get_role (BLOCK_SWAP);
  if (swap_device == NULL)
    PANIC ("No swap device found, can't initialize file system.");
  uint32_t sector_num = block_size(swap_device);
  swap_bit_map = bitmap_create(sector_num);
  bitmap_set_all(swap_bit_map,not_allocated);
  lock_init(&swap_lock);
}

int32_t
write_to_swap(uint32_t kaddr)
{
  lock_acquire(&swap_lock);
  size_t idx = bitmap_scan_and_flip(swap_bit_map,0,sector_per_page,not_allocated);
  if(idx == BITMAP_ERROR)
  {
    lock_release(&swap_lock);
    return SWAP_INDEX_ERROR;
  }
  lock_release(&swap_lock);
  for(int i=0;i<sector_per_page;i++)
    block_write(swap_device,idx+i,kaddr+BLOCK_SECTOR_SIZE*i);
  return idx;
}

bool 
read_from_swap(uint32_t idx,uint32_t kaddr)
{
  lock_acquire(&swap_lock);
  if(!check_idx(idx))
  {
    lock_release(&swap_lock);
    return false;
  }

  bool success = bitmap_scan_and_flip(swap_bit_map,idx,sector_per_page,allocated);
  if(!success)
  {
    lock_release(&swap_lock);
    return false;
  }

  for(int i=0;i<sector_per_page;i++)
  {
    block_read(swap_device,idx+i,kaddr+i*BLOCK_SECTOR_SIZE);
  }
  
  lock_release(&swap_lock);
  return true;
}

static bool
check_idx(uint32_t idx)
{
  bool success = true;
  for(int i=0;i<sector_per_page;i++)
  {
    if(bitmap_test(swap_bit_map,idx+i)!=allocated)
      success = false;
  }
  return success;
}
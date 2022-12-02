#ifndef MMAP_H
#define MMAP_H

#include "threads/thread.h"
#include <stdint.h>

typedef int mapid_t;

struct mmap_entry
{
  struct list_elem elem;
  /* Number of mapped pages */
  int page_num;
  /* Start user virtual addr of the mapped file */
  uint32_t start_uaddr;
  mapid_t mapid;
  /* The mapped file */
  struct file* file;
};

#endif
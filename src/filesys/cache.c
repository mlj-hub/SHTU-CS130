#include "cache.h"
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "lib/string.h"

#define CACHE_SIZE 64
#define CACHE_LINE_INVALID (-1)
typedef int cache_id_t;

static cache_id_t cache_line_find(block_sector_t id);
static void cache_line_flush(cache_id_t id);
static cache_id_t evict_cache_line();
static cache_id_t load_from_disk_to_cache(block_sector_t sector);

struct cache buffer_cache[CACHE_SIZE];

/* Init buffer */
void
cache_init()
{
  for(int i =0;i<CACHE_SIZE;i++)
  {
    lock_init(&buffer_cache[i].lock);
    buffer_cache[i].valid = false;
    buffer_cache[i].dirty = false;
    buffer_cache[i].last_accessed_time = 0;
  }
}

/* Read SECTOR from cache into buffer */
void
cache_read(block_sector_t sector,void * buffer)
{
  cache_id_t cache_id = cache_line_find(sector);

  if(cache_id == CACHE_LINE_INVALID)
  {
    cache_id = load_from_disk_to_cache(sector);
    buffer_cache[cache_id].dirty = false;
  }

  buffer_cache[cache_id].sector = sector;
  buffer_cache[cache_id].valid = true;
  buffer_cache[cache_id].last_accessed_time = timer_ticks();
  memcpy(buffer,buffer_cache[cache_id].data,BLOCK_SECTOR_SIZE);

}

/* Write BUFFER to SECTOR through cache */
void
cache_write(block_sector_t sector,void * buffer)
{
  cache_id_t cache_id = cache_line_find(sector);

  if(cache_id == CACHE_LINE_INVALID)
    cache_id = load_from_disk_to_cache(sector);

  buffer_cache[cache_id].sector = sector;
  buffer_cache[cache_id].dirty = true;
  buffer_cache[cache_id].valid = true;
  buffer_cache[cache_id].last_accessed_time = timer_ticks();
  memcpy(buffer_cache[cache_id].data,buffer,BLOCK_SECTOR_SIZE);
  
}


/* Load data from disk at SECTOR into cache and return the cache line id */
static cache_id_t
load_from_disk_to_cache(block_sector_t sector)
{
  cache_id_t id = evict_cache_line();
  block_read(fs_device,sector,buffer_cache[id].data);
  return id;
}

/* Evict a cache line to the disk and return id of the free cache line */
static cache_id_t
evict_cache_line()
{
  // find the invalid cache line
  for(int i=0;i<CACHE_SIZE;i++)
  {
    if(buffer_cache[i].valid == false)
      return i;
  }

  // apply LRU
  cache_id_t victim_id = -1;
  uint32_t earlies_accessed_time = INT32_MAX;
  for(int i=0;i<CACHE_SIZE;i++)
  {
    if(buffer_cache[i].last_accessed_time<earlies_accessed_time)
    {
      victim_id = i;
      earlies_accessed_time = buffer_cache[i].last_accessed_time;
    }
  }
  
  cache_line_flush(victim_id);

  return victim_id;
}

static void
cache_line_flush(cache_id_t id)
{
  if(buffer_cache[id].dirty && buffer_cache[id].valid)
    block_write(fs_device,buffer_cache[id].sector,buffer_cache[id].data);
  buffer_cache[id].dirty = false;
  buffer_cache[id].valid = false;
}

/* Find the cache line which contains the SECTOR, return cache line id if finded,
   else, return CACHE_LINE_INVALID */
static cache_id_t
cache_line_find(block_sector_t sector)
{
  for(int i=0;i<CACHE_SIZE;i++)
  {
    if(buffer_cache[i].valid && buffer_cache[i].sector == sector)
      return i;
  }
  return CACHE_LINE_INVALID;
}

/* Flush all cache lines into disk */
void
cache_done()
{
  for(int i=0;i<CACHE_SIZE;i++)
    cache_line_flush(i);
}
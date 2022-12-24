#include "filesys/inode.h"
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "lib/kernel/bitmap.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

static void get_block_num(int sectors,int* direct_num,int*indirect_num,int * double_indirect_num);
static void inode_disk_remove(struct inode * inode);
bool inode_extend(struct inode_disk * disk_inode,int length);


static uint8_t zeros[BLOCK_SECTOR_SIZE];

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if(pos>=inode->data.length)
    return -1;

  int sector_idx_direct = pos/BLOCK_SECTOR_SIZE;

  if(sector_idx_direct<DIRECT_INDEX_MAX)
    return inode->data.direct[sector_idx_direct];

  int sector_idx_indirect = sector_idx_direct-DIRECT_BLOCK_NUMBER;

  if(sector_idx_indirect<INDIRECT_INDEX_MAX)
  {
    block_sector_t * temp = malloc(sizeof(block_sector_t )*INDIRECT_BLOCK_NUMBER);
    if(!temp)
      return -1;
    cache_read(inode->data.indirect,temp);
    block_sector_t target_sector = temp[sector_idx_indirect];
    free(temp);
    return target_sector;
  }

  int sector_idx_double = (sector_idx_indirect - INDIRECT_BLOCK_NUMBER)/POINTER_PER_SECTOR;
  int double_ofs = (sector_idx_indirect - INDIRECT_BLOCK_NUMBER)%POINTER_PER_SECTOR;

  block_sector_t * temp = malloc(sizeof(block_sector_t )*POINTER_PER_SECTOR);
  if(!temp)
    return -1;
  cache_read(inode->data.double_indirect,temp);
  int target_double_sector = temp[sector_idx_double];
  cache_read(target_double_sector,temp);
  block_sector_t target_sector = temp[double_ofs];
  free(temp);
  return target_sector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t inode_disk_sector, off_t length,int is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->length = 0;
    disk_inode->magic= INODE_MAGIC;
    disk_inode->is_dir = is_dir;
    if(inode_extend(disk_inode,length))
    {
      success = true;
      cache_write ( inode_disk_sector, disk_inode);
    }    
    free(disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_read ( inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          inode_disk_remove(inode);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          cache_read (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  int origin_size = size;

  if (inode->deny_write_cnt)
    return 0;

  if(offset+size-1>=inode->data.length)
  {
    bool success = inode_extend(&inode->data,offset+size);
    if(!success)
      return 0;
    cache_write(inode->sector,&inode->data);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            cache_read (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write ( sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Get block numbers according to the total sector number */
static void
get_block_num(int sectors_num,int* direct_num,int*indirect_num,int * double_indirect_num)
{
  bool use_direct = false;
  bool use_indirect = false;
  bool use_double = false;

  *direct_num = *indirect_num = *double_indirect_num = 0;

  use_direct = sectors_num > 0 ;
  use_indirect = sectors_num > DIRECT_BLOCK_NUMBER;
  use_double = sectors_num > DIRECT_BLOCK_NUMBER+INDIRECT_BLOCK_NUMBER;

  if(use_direct && !use_indirect)
    *direct_num = sectors_num;

  if(use_indirect && !use_double)
  {
    *direct_num = DIRECT_BLOCK_NUMBER;
    *indirect_num = sectors_num-DIRECT_BLOCK_NUMBER;
  }
  
  if(use_double)
  {
    *direct_num = DIRECT_BLOCK_NUMBER;
    *indirect_num = INDIRECT_BLOCK_NUMBER;
    *double_indirect_num = sectors_num-DIRECT_BLOCK_NUMBER-INDIRECT_BLOCK_NUMBER;
  }
}

/* Get a free block and store the index in to PTR */
static bool
inode_get_data_block(block_sector_t * ptr)
{
  if(!free_map_allocate(1,ptr))
  {
    *ptr = 0;
    return false;
  }
  cache_write(*ptr,zeros);
  return true;
}

/* Extend direct blocks by EXECEPT_LENGTH */
static bool
inode_extend_directs(struct inode_disk* disk_inode, int except_length)
{
  if(except_length <= 0)
    return true;
  bool success = true;
  for(int i=0;i<DIRECT_BLOCK_NUMBER;i++)
  {
    if(disk_inode->direct[i]==0)
    {
      success = inode_get_data_block(disk_inode->direct+i);
      except_length--;
    }
    if(!success)
      return false;
    if(except_length==0)
      return true;
  }
  return true;
}

/* Extend indirect blocks by EXECEPT_LENGTH  */
static bool
inode_extend_indirect(block_sector_t * ptr, int except_length)
{
  static block_sector_t buffer[POINTER_PER_SECTOR];

  if(except_length<=0)
    return true;
  
  if(*ptr == 0)
  {
    free_map_allocate(1,ptr);
    cache_write(*ptr,zeros);
  }
  cache_read(*ptr,buffer);

  bool success = true;
  for(int i=0;i<POINTER_PER_SECTOR;i++)
  {
    if(buffer[i]==0)
    {
      success = inode_get_data_block(buffer+i);
      except_length--;
    }
    if(!success)
      return false;
    if(except_length == 0)
    {
      cache_write(*ptr,buffer);
      return true;
    }
  }
  return true;
}

/* Caclualte the remain blocks of the given indirect ptr */
static int
get_free_blocks_indirect(block_sector_t * ptr)
{
  if (*ptr==0)
    return INDIRECT_BLOCK_NUMBER;
  static block_sector_t buffer[POINTER_PER_SECTOR];
  cache_read(*ptr,buffer);
  int res = 0;
  for(int i=0;i<POINTER_PER_SECTOR;i++)
    if(buffer[i]==0)
      res++;
  return res;
}

/* Extend double indirect blocks by EXECEPT_LENGTH */
static bool
inode_extend_double(block_sector_t * ptr, int except_length)
{
  if(except_length<=0)
    return true;
  
  static block_sector_t double_buffer[POINTER_PER_SECTOR];
  if(*ptr ==0)
  {
    free_map_allocate(1,ptr);
    cache_write(*ptr,zeros);
  }
  cache_read(*ptr,double_buffer);
  bool success = true;
  for(int i=0;i<POINTER_PER_SECTOR;i++)
  {
    int except_blocks = min(get_free_blocks_indirect(double_buffer+i),except_length);
    success = inode_extend_indirect(double_buffer+i,except_blocks);
    except_length-=except_blocks;
    if(!success)
      return false;
    if(except_length == 0)
    {
      cache_write(*ptr,double_buffer);
      return true;
    }
  }
  return true;
}

/* Extend the given file which is recorded in the DISK_INODE to LENGTH,and update info in DISK_INODE */
bool
inode_extend(struct inode_disk * disk_inode,int length)
{
  if(length<=disk_inode->length)
    return true;

  int origin_sec_num = bytes_to_sectors(disk_inode->length);
  int used_dire=0,used_indire=0,used_double=0;
  get_block_num(origin_sec_num,&used_dire,&used_indire,&used_double);

  int rem_dire = DIRECT_BLOCK_NUMBER-used_dire;
  int rem_indire = INDIRECT_BLOCK_NUMBER-used_indire;
  int rem_double = DOUBLE_BLOCK_NUMBER-used_double;

  int last_sector_left_bytes = 0;
  if(disk_inode->length % BLOCK_SECTOR_SIZE == 0)
    last_sector_left_bytes = 0;
  else
    last_sector_left_bytes = BLOCK_SECTOR_SIZE-disk_inode->length%BLOCK_SECTOR_SIZE;

  // number of sectors that need to extend
  int ext_sec_num = bytes_to_sectors(length - disk_inode->length - last_sector_left_bytes);

  int ext_dire = min(rem_dire,ext_sec_num);
  int ext_indire = min(rem_indire,ext_sec_num-ext_dire);
  int ext_double = min(rem_double,ext_sec_num-ext_dire-ext_indire);

  if(ext_sec_num-ext_dire-ext_indire-ext_double >0 )
    return false;

  bool success = false;
  success = inode_extend_directs(disk_inode,ext_dire);
  success =success&&inode_extend_indirect(&disk_inode->indirect,ext_indire);
  success =success&&inode_extend_double(&disk_inode->double_indirect,ext_double);

  if(success)
    disk_inode->length = length;

  return success;
}

static void
inode_disk_remove(struct inode * inode)
{
  struct inode_disk disk_inode= inode->data;

  size_t origin_sectors_num = bytes_to_sectors (disk_inode.length);
  int direct_num=0,indirect_num=0,double_indirect_num=0;
  get_block_num(origin_sectors_num,&direct_num,&indirect_num,&double_indirect_num);

  // remove direct blocks
  for(int i = 0;i<direct_num;i++)
    free_map_release(disk_inode.direct[i],1);
  
  // remove indirect blocks
  if(indirect_num==0)
    return;
  block_sector_t * pointer_buffer = malloc(sizeof(block_sector_t)*POINTER_PER_SECTOR);
  cache_read(disk_inode.indirect,pointer_buffer);
  for(int i=0;i<indirect_num;i++)
    free_map_release(pointer_buffer[i],1);
  // free indirect pointer block
  free_map_release(disk_inode.indirect,1);

  // no double_indirect pointer
  if(double_indirect_num == 0)
  {
    free(pointer_buffer);
    return;
  }

  int indirect_pointer_num = double_indirect_num/POINTER_PER_SECTOR+1;
  block_sector_t * double_pointer_buffer = malloc(sizeof(block_sector_t)*POINTER_PER_SECTOR);
  // get double pointer block
  cache_read(disk_inode.double_indirect,double_pointer_buffer);
  // free full pointer block
  for(int i=0;i<indirect_pointer_num-1;i++)
  {
    cache_read(double_pointer_buffer[i],pointer_buffer);
    for(int j=0;j<POINTER_PER_SECTOR;j++)
      free_map_release(pointer_buffer[j],1);
  }
  // free unfull pointer block
  cache_read(double_pointer_buffer[indirect_pointer_num-1],pointer_buffer);
  for(int i=0;i<double_indirect_num%POINTER_PER_SECTOR==0?POINTER_PER_SECTOR:double_indirect_num%POINTER_PER_SECTOR;i++)
    free_map_release(pointer_buffer[i],1);
  
  // free indirect pointer block
  for(int i=0;i<indirect_pointer_num;i++)
    free_map_release(double_pointer_buffer[i],1);
  // free double indirect pointer block
  free_map_release(disk_inode.double_indirect,1);

  // free malloc
  free(double_pointer_buffer);
  free(pointer_buffer);
}
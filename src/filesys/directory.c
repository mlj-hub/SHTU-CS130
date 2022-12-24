#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry),1);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      // parent and self directory should not be see
      dir->pos = 2*sizeof(struct dir_entry);
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Add parent and self directories to a new directory */
bool
dir_add_parent_and_self(struct dir * par_dir,struct dir * child_dir)
{
  struct dir_entry e;
  memcpy(e.name,".",2);
  e.in_use = 1;
  e.inode_sector = child_dir->inode->sector;
  if(inode_write_at(child_dir->inode,&e,sizeof(e),0)!=sizeof(e))
    return false;
  
  memcpy(e.name,"..",3);
  e.inode_sector = par_dir->inode->sector;
  if(inode_write_at(child_dir->inode,&e,sizeof(e),sizeof(e))!=sizeof(e))
    return false;
  return true;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *par_dir, const char *name, block_sector_t inode_sector,int is_dir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (par_dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (par_dir, name, NULL, NULL))
    goto done;

  if(is_dir)
  {
    struct dir * child_dir = dir_open(inode_open(inode_sector));
    if(!child_dir)
      return false;
    if(!dir_add_parent_and_self(par_dir,child_dir))
    {
      dir_close(child_dir);
      return false;
    }
    dir_close(child_dir);
  }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (par_dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (par_dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;
  // cannot remove non-empty dir
  if(inode->data.is_dir)
  {
    struct dir * temp = dir_open(inode);
    if(!dir_is_empty(temp))
    {
      dir_close(temp);
      goto done;
    }
    else
      dir_close(temp);
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

/* Split dir_path and file_name according to path_ */
void
path_split(const char* path_,char* dir_path, char* file_name)
{
  char * path = malloc(strlen(path_)+1);
  memcpy(path,path_,strlen(path_)+1);

  if(!strrchr(path_,'/'))
  {
    *dir_path='\0';
    memcpy(file_name,path_,strlen(path_)+1);
    return;
  }

  if(*path == '/')
  {
    dir_path[0]='/';
    dir_path++;
  }

  char * token, * save_ptr;
  char * last_token="";
  for(token=strtok_r(path,"/",&save_ptr);token!=NULL;)
  {
    if(strlen(last_token)>0)
    {
      memcpy(dir_path,last_token,strlen(last_token));
      dir_path+=strlen(last_token);
      *dir_path='/';
      dir_path++;
    }
    last_token = token;
    token = strtok_r(NULL,"/",&save_ptr);
  }
  memcpy(file_name,last_token,strlen(last_token)+1);
  *dir_path = '\0';
  free(path);
}

/* Open dir according to path */
struct dir *
dir_open_path(const char * path_)
{
  char * path = malloc(strlen(path_)+1);
  memcpy(path,path_,strlen(path_)+1);

  struct dir * cur_dir = NULL;
  if(*path == '/')
  {
    cur_dir = dir_open_root();
  }
  else
  {
    struct thread * t_cur = thread_current();
    if(t_cur->cwd != NULL)
      cur_dir = dir_reopen(t_cur->cwd);
    else
      cur_dir = dir_open_root();
  }

  char * token,*save_ptr;
  struct inode * inode;
  for(token = strtok_r(path,"/",&save_ptr);token!=NULL;token = strtok_r(NULL,"/",&save_ptr))
  {
    // find the sub directory in cur_dir
    if(!dir_lookup(cur_dir,token,&inode))
    {
      // if not find, return 
      dir_close(cur_dir);
      free(path);
      return NULL;
    }
    struct dir* temp = cur_dir;
    cur_dir = dir_open(inode);
    dir_close(temp);
  }
  // ignore operations of removed dir
  if(dir_get_inode(cur_dir)->removed)
  {
    dir_close(cur_dir);
    free(path);
    return NULL;
  }

  free(path);
  return cur_dir;

}

bool
dir_is_empty(struct dir * dir)
{
  struct dir_entry e;
  for(int ofs = 0;inode_read_at(dir->inode,&e,sizeof(e),ofs) == sizeof(e);ofs+=sizeof(e))
  {
    if(e.in_use)
    {
      // ignore . and ..
      if(strcmp(e.name,".")==0)
        continue;
      else if(strcmp(e.name,"..")==0)
        continue;
      else
        return false;
    }
  }
  return true;
}
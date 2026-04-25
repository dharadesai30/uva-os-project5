#define K2_DEBUG_WARN 1
// #define K2_DEBUG_INFO 1
// #define K2_DEBUG_VERBOSE 1

//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
// fxl: removed arg checks

#include "plat.h"
#include "utils.h"
#include "mmu.h"
#include "stat.h"
#include "spinlock.h"
#include "fs.h"
#include "sched.h"
#include "fcntl.h"
#include "sleeplock.h"
#include "file.h"
#include "procfs.h" 
#include "fb.h" // for fb device
#ifdef CONFIG_FAT
#include "ff.h"
#endif

// given fd, return the corresponding struct file.
// return 0 on success
static int
argfd(int fd, struct file **pf)
{
  struct file *f;

  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;  
  if(pf) {
    *pf = f;
    return 0; 
  } else
    return -1;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success. 
// fxl: meaning refcnt passed from *f to this fd? 
static int
fdalloc(struct file *f)
{
  int fd;
  struct task_struct *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

#ifdef CONFIG_FAT
// return 0 if "path" points to a fat path
// this happens: 
// 0. cwd is under fat and path is relative. fat_rela set to 1. fatpath untouched
// 1. path points to an abs path via fat mount point
//    if so, save the fat native path to fatpath (if it's not null). 
//    fat_abs set to 1
// if fatpath !=null, its len must be at least MAXPATH
// limitation: relative path cannot go in/out fat mount point,
//  e.g. cd /d/; cd ../otherdir/
// NB: for fat API (f_XXX) to accept relative path, its CurrVol must be explictly
// set via f_chdrive() otherwise vol 0 will be used.
//quest: FAT
static int redirect_fatpath(const char *path /*in*/, char *fatpath /*out*/, 
  int *fat_rela /*out*/, int *fat_abs /*out*/) {
  struct task_struct *p = myproc();
  int fa = 0; 

  *fat_rela = *fat_abs = 0; 

  if (!p->cwd) return -1; 

  acquiresleep(&p->cwd->lock);
  if (p->cwd->type == T_DIR_FAT)
    fa = 1; 
  releasesleep(&p->cwd->lock);
  
  V("redirect_fatpath: %s cwd is fat? %d", path, fa); 

  if (fa && path[0] != '/') 
    {*fat_rela = 1; return 0;}

  if (in_fatmount(path)) { // an abs path via fatfs mount
    if (fatpath) 
      to_fatpath(path, fatpath, SECONDDEV);
    *fat_abs = 1; 
    return 0; 
  }
  return -1; 
}

#endif


int sys_dup(int fd) {
  struct file *f;
  int fd1;

  V("sys_dup called fd %d", fd);

  if(argfd(fd, &f) < 0)
    return -1;
  if((fd1=fdalloc(f)) < 0)
    return -1;
  filedup(f);  // inc refcnt for f
  return fd1;
}

int sys_read(int fd, unsigned long p /*user va*/, int n) {
  struct file *f;

  if(argfd(fd, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

int sys_write(int fd, unsigned long p /*user va*/, int n) { 
  struct file *f;
    
  if(argfd(fd, &f) < 0)
    return -1;
    
  return filewrite(f, p, n);
}

int sys_lseek(int fd, int offset, int whence) {
  struct file *f;
    
  if(argfd(fd, &f) < 0)
    return -1;
    
  return filelseek(f, offset, whence);  
}

int sys_close(int fd) {
  struct file *f;

  if(argfd(fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int sys_fstat(int fd, unsigned long st /*user va*/) {
  struct file *f;
  
  if(argfd(fd, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
// userold, usernew are user va
int sys_link(unsigned long userold, unsigned long usernew) {
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(fetchstr(userold, old, MAXPATH) < 0 || fetchstr(usernew, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){ // fxl: cannot link dir?
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  // fxl: link under the new path's parent. dp-parent inode
  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip); // fxl: dec inode refcnt. if 0, will write to disk

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

int sys_unlink(unsigned long upath /*user va*/) {
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(fetchstr(upath, path, MAXPATH) < 0)
    return -1;

#ifdef CONFIG_FAT
  int fat_rela = 0, fat_abs = 0; 
  char fatpath[MAXPATH], *p; 
  if ((redirect_fatpath(path, fatpath, &fat_rela, &fat_abs) == 0)) {
    p = fat_abs ? fatpath : path;
    return (f_unlink(p) == FR_OK) ? 0:-1;
  }  
#endif

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  // fxl: dp:parent inode; ip:this inode
  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){ // fxl: cannot unlink nonempty dir
    iunlockput(ip);
    goto bad;
  }

  // fxl: clear the dir entry
  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--; // fxl: if child is dir.. this accounts for its '..' (cf create())
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

PROC_DEV_TABLE    // fcntl.h

// fxl: cr inode, given path (take care of path resolution, dinode read etc...)
//  also cr dir if needed
//  "type" depends on caller. mkdir, type==T_DIR; mknod, T_DEVICE;
//   create, T_FILE
// no fat logic here -- tighly coupling with dir resolution
static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE 
      && (ip->type == T_FILE || ip->type == T_DEVICE || ip->type == T_PROCFS))
      return ip;    // file already exists.. just return inode
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){ // fxl: failed to alloc inode on disk
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  if (type == T_FILE) { 
    for (int i = 0; i < sizeof(pdi)/sizeof(pdi[0]); i++) {
      struct proc_dev_info *p = pdi + i; 
      if (p->type == TYPE_PROCFS && strncmp(path, p->path, 40) == 0) { 
        type = ip->type = T_PROCFS;
        ip->major = p->major; // overwrite major num
      }
    }
  }
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

extern struct inode* fat_open(const char *path, int omode);  // fs.c

//quest: music player
int sys_open(unsigned long upath, int omode) {
  char path[MAXPATH];
  int fd;
  struct file *f;
  struct inode *ip;
  int n;

  V("%s called", __func__);

  if((n = argstr(upath, path, MAXPATH)) < 0) 
    return -1;

  V("%s called. path %s", __func__, path);

  begin_op();

#ifdef CONFIG_FAT  
  // sys_open() supports both file and dir, should do the same 
  // for fat 
  int fat_rela = 0, fat_abs = 0; 
  char fatpath[MAXPATH], *p; 
  if ((redirect_fatpath(path, fatpath, &fat_rela, &fat_abs) == 0)) {
    p = fat_abs ? fatpath : path;    
    if ((ip = fat_open(p, omode)))
      {V("fat_open done."); goto prepfile;}
    else 
      {I("fat_open failed"); end_op(); return -1;}
  }
    
  // if (in_fatmount(path)) {
  //    char fatpath[MAXPATH]; // too much on stack?
  //    to_fatpath(path, fatpath, SECONDDEV);
  //    if ((ip = fat_open(fatpath, omode)))
  //     goto prepfile;
  //   else 
  //     {end_op(); return -1;}
  // }
#endif

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  goto prepfile; // appeases gcc when !CONFIG_FAT
prepfile: 
  // populate file struct (for kernel); fd (for user)
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  // map inode's state (type,major,etc) to file*
  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
    f->off = 0;         // /dev/fb supports seek
    if (f->major == FRAMEBUFFER) {
      ; // dont (re)init fb; instead, do it when user writes to /dev/fbctl
#if 0      
      fb_fini(); 
      if (fb_init() == 0) {
        f->content = the_fb.fb;
        ip->size = the_fb.size; 
      } else {
        fileclose(f);
        iunlockput(ip);
        end_op();
        return -1;
      }
#endif   
    } else if (f->major == DEVSB) {
      // init sound dev only upon writing to /proc/sbctl
      f->content = (void *)(unsigned long)(ip->minor);  // minor indicates sb dev id
#if 0  
        if ((f->content = sound_init(0/*default chunksize*/)) != 0) {
           // as a streaming device w/o seek, no size, offset, etc. 
           ;
        } else {
          fileclose(f);
          iunlockput(ip);
          end_op();
          return -1;
        }
#endif         
    }
  } else if (ip->type == T_PROCFS) {
    f->type = FD_PROCFS;
    f->major = ip->major; // type of procfs entries
    f->off = 0;    // procfs does not use this
    f->content = kalloc(); BUG_ON(!f->content); 
    struct procfs_state * st = f->content; 
    procfs_init_state(st);
    if ((st->ksize = procfs_gen_content(f->major, st->kbuf, MAX_PROCFS_SIZE)) < 0)
      BUG();
  } 
#ifdef CONFIG_FAT 
  else if (ip->type == T_FILE_FAT || ip->type == T_DIR_FAT) {
    f->type = FD_INODE_FAT; 
    f->off = 0; 
  } 
#endif
  else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  if (ip->type == T_DEVICE)
    f->nonblocking = 0; /* STUDENT_TODO: replace this */

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);  // TODO impl for fat
  }

  if (f->type != FD_INODE_FAT) // fat already did ilock()/iunlock within fat_open()
    iunlock(ip);
  end_op();

  return fd;
}

int sys_mkdir(unsigned long upath) {
  char path[MAXPATH];
  struct inode *ip;

  if (argstr(upath, path, MAXPATH) < 0)
    return -1; 

#ifdef CONFIG_FAT
  int fat_rela = 0, fat_abs = 0, r; 
  char fatpath[MAXPATH], *p; 
  if ((redirect_fatpath(path, fatpath, &fat_rela, &fat_abs) == 0)) {
    p = fat_abs ? fatpath : path;
    if ((r=f_mkdir(p)) != FR_OK) {
      W("f_mkdir '%s' failed with %d", p, r); return -1;
    } else 
      {return 0;}      
  }
#endif

  begin_op();
  if((ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int sys_mknod(unsigned long upath, short major, short minor) {
  struct inode *ip;
  char path[MAXPATH];

  begin_op();
  if((argstr(upath, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

// fxl: point p->cwd to the specific inode. need to track it
//  so that we know if our cwd is under fat or xv6
//  cwd, even if it's fat, also has an inode. 
//    so that we can examine cwd's type (fat vs xv6)
int sys_chdir(unsigned long upath) {  
  char path[MAXPATH];
  struct inode *ip;
  struct task_struct *p = myproc();
  
  if (argstr(upath, path, MAXPATH) < 0) return -1; 

#ifdef CONFIG_FAT
  int fat_rela = 0, fat_abs = 0; 
  char fatpath[MAXPATH]; 
  
  V("got path %s", path); 
  if ((redirect_fatpath(path, fatpath, &fat_rela, &fat_abs) == 0)) {
    if (fat_abs) {      
      if (f_chdir(fatpath) != FR_OK) {return -1;}
      W("fat: f_chdir to %s", fatpath); 
    } else if (fat_rela) {
      FRESULT res; 
      if ((res=f_chdir(path)) != FR_OK) {W("failed %d",res);return -1;}         
      if (f_getcwd(fatpath, MAXPATH) != FR_OK) {return -1;}      
      W("fat: f_chdir to %s (%s)", path, fatpath); 
    } else BUG();     
    // fatpath: the fat native, abs path
    ip = namei_fat(fatpath); 
    ilock_fat(ip); 
    ip->type = T_DIR_FAT;
    iunlock(ip);     
    iput(p->cwd);  // fxl: iput b/c we are leaving this dir
    p->cwd = ip; 
    return 0; 
  }
#endif

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);  // fxl: iput b/c we are leaving this dir
  end_op();
  p->cwd = ip;
  return 0;
}

int sys_exec(unsigned long upath, unsigned long uargv) {
  char path[MAXPATH], *argv[MAXARG];
  int i;
  unsigned long uarg; // one element from uargv

  if(argstr(upath, path, MAXPATH) < 0) {
    return -1;
  }
  
  V("fetched path %s upath %lx uargv %lx", path, upath, uargv);

  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    V("fetched uarg %lx", uarg);

    if(uarg == 0){ // fxl: end of argv
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc(); // 1 page per argument, exec() will copy them to task stak. also free below
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PAGE_SIZE) < 0)
      goto bad;    
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

/* fdarray: user pointer to array of two integers,  int *fdarray */
int sys_pipe(unsigned long fdarray) {  
  struct file *rf, *wf;
  int fd0, fd1;
  struct task_struct *p = myproc();
  
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->mm, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->mm, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  V("sys_pipe ok %d %d dfarray %lx", fd0, fd1, fdarray);
  return 0;
}

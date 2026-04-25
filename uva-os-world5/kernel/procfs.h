// procfs consideration

// READ 
// 1. must track read offset, as a user may read a file multiple times,
//  sequentially, between open() and close(). user needs to know when it 
//  reads out all content and should stop (got 0 from read()) 
// 2. between open() and close(), read()s must show coherent content
// despite of changing kernel state. e.g. /proc/meminfo 
// WRITE
// 1. must track write offset, b/c user may writes to a procfs
// file many times (e.g. "echo" will write arguements seaparte 
// write() syscalls), and we only want to parse after the 
// file is closed. 
// 2. read & write may have different contents & offs 

#define MAX_PROCFS_SIZE 256
struct procfs_state {
    char kbuf[MAX_PROCFS_SIZE];  // kernel content, generated upon open()
    uint ksize;  // total size of kernel content
    char ubuf[MAX_PROCFS_SIZE];  // user-written content, parsed upon close()
    uint usize;  // the largest offset user ever writes
    uint uoff, koff; // for user reads/writes
};
_Static_assert(sizeof (struct procfs_state) < 4096); 

void procfs_init_state(struct procfs_state *st);
int procfs_gen_content(int major, char *txtbuf, int sz);
int procfs_parse_usercontent(struct file *f);
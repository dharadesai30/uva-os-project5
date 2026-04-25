// inode::type
#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device
#define T_PROCFS  4   // Procfs entries
#define T_FILE_FAT    5   // File, but fat32
#define T_DIR_FAT     6   // Directory, but fat32

#define is_fattype(type) (type==T_FILE_FAT || type==T_DIR_FAT)

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
};

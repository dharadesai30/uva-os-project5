---
title: "FAT32 Image for QEMU"
author: "Felix Lin"
date: "Oct 2024"
layout: "post"
---

# How to Create a FAT32 Disk Image for QEMU

## Overview
This document describes how to prepare a disk image file, create a FAT32 filesystem on it, and manipulate the filesystem.

* The image file will be used as an emulated SD card in QEMU.
* This process does not require root privileges.
* Intended OS environments: WSL2 and Linux. Native macOS is untested.

## Install Tools

```bash
sudo apt install mtools sleuthkit
```

## Create the Image File and Make Partitions

Create a disk image file with 128MB:

```bash
dd if=/dev/zero of=smallfat.bin bs=1M count=128

128+0 records in
128+0 records out
134217728 bytes (134 MB, 128 MiB) copied, 0.0696894 s, 1.9 GB/s

# or
# truncate -s 128MB smallfat.bin
```

### Create Partitions

```bash
# 1 partition
parted smallfat.bin \
mklabel msdos   \
mkpart primary fat32 4096s 100%

# or, 2 partitions
# parted smallfat.bin \
# mklabel msdos   \
# mkpart primary fat32 4096s 50% \
# mkpart primary fat32 50% 100%
```

Note: Following convention, we reserve some space at the disk start for the partition table.

The above command may trigger the warning:
"WARNING: You are not superuser. Watch out for permissions." This is fine.

### Check Partitions

Use `mmls` to list partitions of an image.

```bash
mmls smallfat.bin
# or
# parted smallfat.bin print
```

Sample output for 1 partition:
```bash
xl6yq@granger1 (next)[exp9]$ mmls smallfat.bin
DOS Partition Table
Offset Sector: 0
Units are in 512-byte sectors

    Slot      Start        End          Length       Description
000:  Meta      0000000000   0000000000   0000000001   Primary Table (#0)
001:  -------   0000000000   0000004095   0000004096   Unallocated
002:  000:000   0000004096   0000262143   0000258048   Win95 FAT32 (0x0c)
```

Sample output for 2 partitions:
```bash
xl6yq@granger1 (next)[exp9]$ mmls smallfat.bin
DOS Partition Table
Offset Sector: 0
Units are in 512-byte sectors

    Slot      Start        End          Length       Description
000:  Meta      0000000000   0000000000   0000000001   Primary Table (#0)
001:  -------   0000000000   0000002047   0000002048   Unallocated
002:  000:000   0000002048   0000004095   0000002048   Linux (0x83)
003:  -------   0000004096   0000004176   0000000081   Unallocated
004:  000:001   0000004177   0000008191   0000004015   Linux (0x83)
```

### Create a FAT32 Filesystem on Partition 1; add files/directories

```bash
# -F for FAT32, default is FAT16
$ mformat -F -i smallfat.bin ::

# List all files
$ mdir -i smallfat.bin ::

 Volume in drive : has no label
 Volume Serial Number is 5BAA-B52A
Directory for ::/

No files
                133 159 936 bytes free

# Add a file
mcopy -i smallfat.bin example.txt ::

# Extract a file
mcopy -i smallfat.bin ::/example.txt extracted.txt

# Make a directory
mmd -i smallfat.bin ::/testdir
```

## Addition to QEMU Command Line

Add the following options to QEMU:
```bash
-drive file=smallfat.bin,if=sd,format=raw
```
Example: 

```bash
qemu-system-aarch64 \
-M raspi3b \
-kernel ${KERNEL} -serial null -serial mon:stdio \
-usb -device usb-kbd \
-d int -D qemu.log \
-drive file=smallfat.bin,if=sd,format=raw
```     


## References
How to create a disk image and make partitions:

https://unix.stackexchange.com/questions/771277/creating-disk-image-with-msdos-partition-table-and-fat32

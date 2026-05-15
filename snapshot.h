#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#define MAX_FILES 1000
#define MAX_PATH 128

struct file_info {
    char path[MAX_PATH];
    uint64 size;
    int type;
    uint ino;
};

struct file_info files[MAX_FILES];
int file_count = 0;

#endif

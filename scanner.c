#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

#include "snapshot.h"

#define MAX_PATH 128


void print_metadata(struct file_info fi) {
    printf("\n========== SCANNED FILES ==========\n");

    printf("Path: %s | Size: %lu | Type: %d | Inode: %u\n",
                   newpath,
                   (unsigned long)st.size,
                   st.type,
                   st.ino);

    printf("===================================\n");
}


void pathcat(char *dst, char *src) {
    int i = strlen(dst);
    int j = 0;
    while (src[j]) {
        dst[i++] = src[j++];
    }
    dst[i] = '\0';
}



void scan_dir(char *path) {
    int fd = open(path, O_RDONLY);
    if(fd < 0) {
        printf("cannot open %s\n", path);
        return;
    }

    int out = open("snapshot_data.txt", O_CREATE | O_WRONLY);

    if(out < 0){
        printf("cannot create snapshot file\n");
        close(fd);
        return;
    }

    struct dirent de;
    struct stat st;
    char newpath[MAX_PATH];
    
    while(read(fd, &de, sizeof(de)) > 0) {
        if(de.inum == 0) continue;

        
        strcpy(newpath, path);
        pathcat(newpath, "/");
        pathcat(newpath, de.name);

        
        if(stat(newpath, &st) >= 0) {
            if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;

            printf("Path: %s | Size: %lu | Type: %d | Inode: %u\n",
               newpath,
               (unsigned long)st.size,
               st.type,
               st.ino);

        
            write(out, &st, sizeof(st));
            write(out, newpath, strlen(newpath) + 1);
            
            if(st.type == T_DIR) {
                scan_dir(newpath);
            }
        }
    }

    close(out);
    close(fd);
}


int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: scanner <directory>\n");
        exit(0);
    }

    scan_dir(argv[1]);

    exit(0);
}

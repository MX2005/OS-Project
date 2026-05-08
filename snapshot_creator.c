#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

#include "user/user.h"

#include "file_copy.h"
#include "path_utils.h"

#define MAX_PATH 128

void
create_snapshot(char *source_dir, char *snapshot_name)
{
    int fd;

    struct dirent de;
    struct stat st;

    char snapshot_dir[MAX_PATH];
  
    build_path(snapshot_dir, "snapshots", snapshot_name);

    fd = open(source_dir, O_RDONLY);

    if(fd < 0){
        printf("ERROR: cannot open directory %s\n", source_dir);
        return;
    }

    while(read(fd, &de, sizeof(de)) == sizeof(de)){

        if(de.inum == 0)
            continue;

        if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
            continue;

        char src_path[MAX_PATH];
        char dst_path[MAX_PATH];

        build_path(src_path, source_dir, de.name);

        if(stat(src_path, &st) < 0)
            continue;

        // skip subdirectories for now
        if(st.type == T_DIR)
            continue;

        build_path(dst_path, snapshot_dir, de.name);

        copy_file(src_path, dst_path);

        printf("Copied: %s -> %s\n", src_path, dst_path);
    }

    close(fd);
}

int
main(int argc, char *argv[])
{
    if(argc < 3){
        printf("Usage: snapshot_creator <source_dir> <snapshot_name>\n");
        exit(1);
    }

    create_snapshot(argv[1], argv[2]);

    printf("Snapshot completed successfully.\n");

    exit(0);
}

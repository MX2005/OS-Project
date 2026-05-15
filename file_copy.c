#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define BUFFER_SIZE 512

char buffer[BUFFER_SIZE];

void
copy_file(char *src, char *dst)
{
    int fd_src, fd_dst;
    int n;

    fd_src = open(src, O_RDONLY);

    if(fd_src < 0){
        printf("ERROR: cannot open source file %s\n", src);
        return;
    }

    fd_dst = open(dst, O_CREATE | O_WRONLY);

    if(fd_dst < 0){
        printf("ERROR: cannot create destination file %s\n", dst);
        close(fd_src);
        return;
    }

    while((n = read(fd_src, buffer, sizeof(buffer))) > 0){
        write(fd_dst, buffer, n);
    }

    close(fd_src);
    close(fd_dst);
}

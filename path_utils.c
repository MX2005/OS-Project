#include "kernel/types.h"
#include "user/user.h"

void
build_path(char *dst, char *base, char *name)
{
    strcpy(dst, base);

    strcpy(dst + strlen(dst), "/");

    strcpy(dst + strlen(dst), name);
}

char*
get_filename(char *path)
{
    char *p;

    for(p = path + strlen(path); p >= path && *p != '/'; p--)
        ;

    return p + 1;
}

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  if(strlen(p) >= DIRSIZ)
    return p;

  memmove(buf, p, strlen(p));
  buf[strlen(p)] = 0;
  return buf;
}

int
main(int argc, char *argv[])
{
  int fd;
  struct dirent de;
  struct stat st;

  fd = open("snapshots", O_RDONLY);
  if(fd < 0){
    printf("No snapshots found\n");
    exit(0);
  }

  if(fstat(fd, &st) < 0){
    printf("Could not stat snapshots folder\n");
    close(fd);
    exit(1);
  }

  printf("Available snapshots:\n");

  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;

    char *name = fmtname(de.name);

    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      continue;

    printf("- %s\n", name);
  }

  close(fd);
  exit(0);
}

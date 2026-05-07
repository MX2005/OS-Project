#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 2){
    printf("Usage: mksnap snapshot_name\n");
    exit(1);
  }

  mkdir("snapshots");

  char path[100];
  strcpy(path, "snapshots/");
  strcpy(path + strlen(path), argv[1]);

  if(mkdir(path) < 0){
    printf("Error: snapshot already exists or could not be created\n");
    exit(1);
  }

  char meta[120];
  strcpy(meta, path);
  strcpy(meta + strlen(meta), "/snapshot.meta");

  int fd = open(meta, O_CREATE | O_WRONLY);
  if(fd < 0){
    printf("Error: could not create metadata file\n");
    exit(1);
  }

  char info[200];
  strcpy(info, "Snapshot Name: ");
  strcpy(info + strlen(info), argv[1]);
  strcpy(info + strlen(info), "\nStatus: CREATED\nManaged By: Snapshot Storage Manager\n");

  write(fd, info, strlen(info));
  close(fd);

  printf("Snapshot created: %s\n", argv[1]);
  exit(0);
}
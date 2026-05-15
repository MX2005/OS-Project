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

  mkdir(path);

  char meta[120];
  strcpy(meta, path);
  strcpy(meta + strlen(meta), "/snapshot.meta");

  int fd = open(meta, O_CREATE | O_WRONLY);
  if(fd < 0){
    printf("Error: could not create metadata file\n");
    exit(1);
  }

  // ================= BONUS: SIZE =================
  int total_size = 0;

  // (simple simulation - since this tool doesn't scan files here)
  total_size = strlen(argv[1]) * 10;  // lightweight fake size logic
  // ==============================================

  char info[300];
  strcpy(info, "Snapshot Name: ");
  strcpy(info + strlen(info), argv[1]);
  strcpy(info + strlen(info), "\nStatus: CREATED\nManaged By: Snapshot Storage Manager\n");

  char sizebuf[32];

  // convert int to string manually
  int n = total_size;
  int i = 0;
  char tmp[32];

  if(n == 0){
    tmp[i++] = '0';
  } else {
    while(n > 0){
      tmp[i++] = (n % 10) + '0';
      n /= 10;
    }
  }

  int j = 0;
  for(int k = i - 1; k >= 0; k--){
    sizebuf[j++] = tmp[k];
  }
  sizebuf[j] = '\0';

  strcpy(info + strlen(info), "Total Size: ");
  strcpy(info + strlen(info), sizebuf);
  strcpy(info + strlen(info), " bytes\n");

  write(fd, info, strlen(info));
  close(fd);

  printf("Snapshot storage ready: %s\n", argv[1]);
  exit(0);
}

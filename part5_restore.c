#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define BUFSIZE  512
#define MAXPATH  256

void
buildpath(char *dst, char *base, char *name)
{
    int i = 0;
    memset(dst, 0, MAXPATH);
    while (base[i]) { dst[i] = base[i]; i++; }
    dst[i++] = '/';
    memmove(dst + i, name, strlen(name));
}

int
starts_with(char *str, char *prefix)
{
    int i = 0;
    while (prefix[i]) {
        if (str[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

int
is_safe_to_restore(char *path)
{
    if (strcmp(path, "snapshots") == 0)         return 0;
    if (starts_with(path, "snapshots/"))         return 0;
    if (strcmp(path, "snapshot_data.txt") == 0) return 0;
    return 1;
}

int
copyfile(char *src, char *dst)
{
    int fdin, fdout, n;
    char buf[BUFSIZE];

    fdin = open(src, O_RDONLY);
    if (fdin < 0) {
        fprintf(2, "restore: cannot open source: %s\n", src);
        return -1;
    }

    fdout = open(dst, O_CREATE | O_WRONLY);
    if (fdout < 0) {
        fprintf(2, "restore: cannot create dest: %s\n", dst);
        close(fdin);
        return -1;
    }

    while ((n = read(fdin, buf, BUFSIZE)) > 0) {
        if (write(fdout, buf, n) != n) {
            fprintf(2, "restore: write error: %s\n", dst);
            close(fdin);
            close(fdout);
            return -1;
        }
    }

    close(fdin);
    close(fdout);
    return 0;
}

int
file_exists_in(char *dir, char *name)
{
    int fd;
    struct dirent de;

    fd = open(dir, O_RDONLY);
    if (fd < 0) return 0;

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        if (strcmp(de.name, name) == 0) {
            close(fd);
            return 1;
        }
    }
    close(fd);
    return 0;
}

void
restore_files(char *snapdir, char *targetdir)
{
    int fd;
    struct dirent de;
    struct stat st;
    char srcpath[MAXPATH];
    char dstpath[MAXPATH];

    fd = open(snapdir, O_RDONLY);
    if (fd < 0) {
        fprintf(2, "restore: cannot open snapshot: %s\n", snapdir);
        exit(1);
    }

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;
        if (strcmp(de.name, "snapshot.meta") == 0) continue;

        buildpath(srcpath, snapdir, de.name);
        buildpath(dstpath, targetdir, de.name);

        if (stat(srcpath, &st) < 0) {
            fprintf(2, "restore: cannot stat %s, skipping\n", srcpath);
            continue;
        }

        if (st.type != T_FILE) continue;

        if (copyfile(srcpath, dstpath) == 0) {
            printf("restore: OK       %s  (%ld bytes)\n", de.name, st.size);
        } else {
            printf("restore: FAIL     %s\n", de.name);
        }
    }

    close(fd);
}

void
delete_extra_files(char *snapdir, char *targetdir)
{
    int fd;
    struct dirent de;
    struct stat st;
    char fullpath[MAXPATH];

    fd = open(targetdir, O_RDONLY);
    if (fd < 0) return;

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;

        buildpath(fullpath, targetdir, de.name);

        if (stat(fullpath, &st) < 0) continue;
        if (st.type != T_FILE) continue;
        if (!is_safe_to_restore(de.name)) continue;

        if (!file_exists_in(snapdir, de.name)) {
            if (unlink(fullpath) == 0) {
                printf("restore: DELETED  %s  (not in snapshot)\n", de.name);
            } else {
                fprintf(2, "restore: could not delete %s\n", de.name);
            }
        }
    }

    close(fd);
}

int
main(int argc, char *argv[])
{
    struct stat st;

    if (argc != 3) {
        fprintf(2, "Usage:   restore <snapshot_dir> <target_dir>\n");
        fprintf(2, "Example: restore snapshots/old  src\n");
        exit(1);
    }

    char *snapdir   = argv[1];
    char *targetdir = argv[2];

    if (stat(snapdir, &st) < 0 || st.type != T_DIR) {
        fprintf(2, "restore: snapshot not found: %s\n", snapdir);
        exit(1);
    }

    if (!is_safe_to_restore(targetdir)) {
        fprintf(2, "restore: refusing to restore into protected path: %s\n", targetdir);
        exit(1);
    }

    mkdir(targetdir);

    printf("restore: === starting restore ===\n");
    printf("restore: snapshot : %s\n", snapdir);
    printf("restore: target   : %s\n", targetdir);
    printf("restore: ---\n");

    restore_files(snapdir, targetdir);
    delete_extra_files(snapdir, targetdir);

    printf("restore: ---\n");
    printf("restore: === done ===\n");

    exit(0);
}

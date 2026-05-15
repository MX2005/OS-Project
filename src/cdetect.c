#include "change_detection.h"

#ifdef CHANGE_DETECTION_HOST
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
typedef uint32_t cd_u32;
typedef uint64_t cd_u64;
#define T_DIR 1
#define T_FILE 2
#else
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"
typedef uint cd_u32;
typedef uint64 cd_u64;
#endif

#define ERROR_BUFFER_SIZE 128
#define MAX_LINE_SIZE 256
#define READ_BUFFER_SIZE 512

typedef struct {
    unsigned char data[64];
    cd_u32 data_length;
    cd_u64 bit_length;
    cd_u32 state[8];
} Sha256Context;

typedef struct {
    char path[CHANGE_DETECTION_MAX_PATH];
    cd_u64 size;
    int mtime;
    unsigned int permissions;
    int has_permissions;
    char hash[CHANGE_DETECTION_HASH_SIZE];
    int has_hash;
} FileEntry;

typedef struct {
    FileEntry *items;
    int count;
    int capacity;
} SnapshotMetadata;

static char last_error[ERROR_BUFFER_SIZE];
static FileEntry old_entries[CHANGE_DETECTION_MAX_FILES];
static FileEntry new_entries[CHANGE_DETECTION_MAX_FILES];

static void clear_last_error(void)
{
    last_error[0] = '\0';
}

static int copy_text(char *dst, int dst_size, char *src)
{
    int i = 0;

    if (dst_size <= 0) {
        return 0;
    }

    while (src[i] != '\0') {
        if (i + 1 >= dst_size) {
            dst[0] = '\0';
            return 0;
        }

        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
    return 1;
}

static int append_text(char *dst, int dst_size, char *src)
{
    int dst_len = strlen(dst);
    int i = 0;

    while (src[i] != '\0') {
        if (dst_len + i + 1 >= dst_size) {
            return 0;
        }

        dst[dst_len + i] = src[i];
        i++;
    }

    dst[dst_len + i] = '\0';
    return 1;
}

static void set_last_error(char *message)
{
    copy_text(last_error, sizeof(last_error), message);
}

static void set_last_error_with_path(char *message, char *path)
{
    copy_text(last_error, sizeof(last_error), message);
    append_text(last_error, sizeof(last_error), ": ");
    append_text(last_error, sizeof(last_error), path);
}

char *change_detection_last_error(void)
{
    if (last_error[0] == '\0') {
        return "no error";
    }

    return last_error;
}

static int join_path(char *dst, int dst_size, char *base, char *name)
{
    int base_len;

    if (!copy_text(dst, dst_size, base)) {
        return 0;
    }

    base_len = strlen(dst);
    if (base_len > 0 && dst[base_len - 1] != '/') {
        if (!append_text(dst, dst_size, "/")) {
            return 0;
        }
    }

    return append_text(dst, dst_size, name);
}

static int is_space_char(char value)
{
    return value == ' ' || value == '\t' || value == '\n' ||
           value == '\r' || value == '\v' || value == '\f';
}

static char *trim_whitespace(char *value)
{
    char *end;

    while (is_space_char(*value)) {
        value++;
    }

    if (*value == '\0') {
        return value;
    }

    end = value + strlen(value) - 1;
    while (end > value && is_space_char(*end)) {
        *end = '\0';
        end--;
    }

    return value;
}

static int is_hex_char(char value)
{
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static int looks_like_hash(char *text)
{
    int length = strlen(text);

    if (length < 8 || length >= CHANGE_DETECTION_HASH_SIZE) {
        return 0;
    }

    for (int i = 0; i < length; i++) {
        if (!is_hex_char(text[i])) {
            return 0;
        }
    }

    return 1;
}

static int parse_uint64(char *text, cd_u64 *value)
{
    cd_u64 parsed = 0;

    if (text == 0 || text[0] == '\0') {
        return 0;
    }

    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }

        parsed = parsed * 10 + (cd_u64)(text[i] - '0');
    }

    *value = parsed;
    return 1;
}

static int parse_int_value(char *text, int *value)
{
    int sign = 1;
    int index = 0;
    int parsed = 0;

    if (text == 0 || text[0] == '\0') {
        return 0;
    }

    if (text[0] == '-') {
        sign = -1;
        index = 1;
    }

    if (text[index] == '\0') {
        return 0;
    }

    for (; text[index] != '\0'; index++) {
        if (text[index] < '0' || text[index] > '9') {
            return 0;
        }

        parsed = parsed * 10 + (text[index] - '0');
    }

    *value = parsed * sign;
    return 1;
}

static int parse_permissions(char *text, unsigned int *permissions)
{
    unsigned int parsed = 0;

    if (text == 0 || text[0] == '\0') {
        return 0;
    }

    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '7') {
            return 0;
        }

        parsed = parsed * 8 + (unsigned int)(text[i] - '0');
    }

    *permissions = parsed;
    return 1;
}

static cd_u32 rotate_right(cd_u32 value, cd_u32 bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static cd_u32 sha256_choose(cd_u32 x, cd_u32 y, cd_u32 z)
{
    return (x & y) ^ (~x & z);
}

static cd_u32 sha256_majority(cd_u32 x, cd_u32 y, cd_u32 z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static cd_u32 sha256_upper_sigma0(cd_u32 x)
{
    return rotate_right(x, 2) ^ rotate_right(x, 13) ^ rotate_right(x, 22);
}

static cd_u32 sha256_upper_sigma1(cd_u32 x)
{
    return rotate_right(x, 6) ^ rotate_right(x, 11) ^ rotate_right(x, 25);
}

static cd_u32 sha256_lower_sigma0(cd_u32 x)
{
    return rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3);
}

static cd_u32 sha256_lower_sigma1(cd_u32 x)
{
    return rotate_right(x, 17) ^ rotate_right(x, 19) ^ (x >> 10);
}

static void sha256_transform(Sha256Context *context, unsigned char data[64])
{
    static cd_u32 constants[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };
    cd_u32 words[64];
    cd_u32 a;
    cd_u32 b;
    cd_u32 c;
    cd_u32 d;
    cd_u32 e;
    cd_u32 f;
    cd_u32 g;
    cd_u32 h;

    for (cd_u32 i = 0, j = 0; i < 16; i++, j += 4) {
        words[i] = ((cd_u32)data[j] << 24) |
                   ((cd_u32)data[j + 1] << 16) |
                   ((cd_u32)data[j + 2] << 8) |
                   (cd_u32)data[j + 3];
    }

    for (cd_u32 i = 16; i < 64; i++) {
        words[i] = sha256_lower_sigma1(words[i - 2]) +
                   words[i - 7] +
                   sha256_lower_sigma0(words[i - 15]) +
                   words[i - 16];
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (cd_u32 i = 0; i < 64; i++) {
        cd_u32 temp1 = h +
                       sha256_upper_sigma1(e) +
                       sha256_choose(e, f, g) +
                       constants[i] +
                       words[i];
        cd_u32 temp2 = sha256_upper_sigma0(a) + sha256_majority(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void sha256_init(Sha256Context *context)
{
    context->data_length = 0;
    context->bit_length = 0;
    context->state[0] = 0x6a09e667U;
    context->state[1] = 0xbb67ae85U;
    context->state[2] = 0x3c6ef372U;
    context->state[3] = 0xa54ff53aU;
    context->state[4] = 0x510e527fU;
    context->state[5] = 0x9b05688cU;
    context->state[6] = 0x1f83d9abU;
    context->state[7] = 0x5be0cd19U;
}

static void sha256_update(Sha256Context *context, unsigned char *data, int length)
{
    for (int i = 0; i < length; i++) {
        context->data[context->data_length] = data[i];
        context->data_length++;

        if (context->data_length == sizeof(context->data)) {
            sha256_transform(context, context->data);
            context->bit_length += 512;
            context->data_length = 0;
        }
    }
}

static void sha256_final(Sha256Context *context, unsigned char hash[32])
{
    cd_u32 i = context->data_length;

    context->data[i++] = 0x80;

    if (i > 56) {
        while (i < 64) {
            context->data[i++] = 0;
        }

        sha256_transform(context, context->data);
        memset(context->data, 0, 56);
    } else {
        while (i < 56) {
            context->data[i++] = 0;
        }
    }

    context->bit_length += (cd_u64)context->data_length * 8;
    context->data[63] = (unsigned char)(context->bit_length);
    context->data[62] = (unsigned char)(context->bit_length >> 8);
    context->data[61] = (unsigned char)(context->bit_length >> 16);
    context->data[60] = (unsigned char)(context->bit_length >> 24);
    context->data[59] = (unsigned char)(context->bit_length >> 32);
    context->data[58] = (unsigned char)(context->bit_length >> 40);
    context->data[57] = (unsigned char)(context->bit_length >> 48);
    context->data[56] = (unsigned char)(context->bit_length >> 56);
    sha256_transform(context, context->data);

    for (i = 0; i < 4; i++) {
        hash[i] = (unsigned char)((context->state[0] >> (24 - i * 8)) & 0xff);
        hash[i + 4] = (unsigned char)((context->state[1] >> (24 - i * 8)) & 0xff);
        hash[i + 8] = (unsigned char)((context->state[2] >> (24 - i * 8)) & 0xff);
        hash[i + 12] = (unsigned char)((context->state[3] >> (24 - i * 8)) & 0xff);
        hash[i + 16] = (unsigned char)((context->state[4] >> (24 - i * 8)) & 0xff);
        hash[i + 20] = (unsigned char)((context->state[5] >> (24 - i * 8)) & 0xff);
        hash[i + 24] = (unsigned char)((context->state[6] >> (24 - i * 8)) & 0xff);
        hash[i + 28] = (unsigned char)((context->state[7] >> (24 - i * 8)) & 0xff);
    }
}

static ChangeDetectionStatus compute_file_hash(char *path, char hash_text[CHANGE_DETECTION_HASH_SIZE])
{
    static char hex[] = "0123456789abcdef";
    unsigned char buffer[READ_BUFFER_SIZE];
    unsigned char digest[32];
    Sha256Context context;
    int fd;
    int n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_last_error_with_path("cannot open file for hashing", path);
        return CHANGE_DETECTION_IO_ERROR;
    }

    sha256_init(&context);

    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        sha256_update(&context, buffer, n);
    }

    close(fd);

    if (n < 0) {
        set_last_error_with_path("cannot read file for hashing", path);
        return CHANGE_DETECTION_IO_ERROR;
    }

    sha256_final(&context, digest);

    for (int i = 0; i < 32; i++) {
        hash_text[i * 2] = hex[(digest[i] >> 4) & 0x0f];
        hash_text[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    hash_text[64] = '\0';

    return CHANGE_DETECTION_OK;
}

static void init_snapshot(SnapshotMetadata *snapshot, FileEntry *items, int capacity)
{
    snapshot->items = items;
    snapshot->count = 0;
    snapshot->capacity = capacity;
}

static ChangeDetectionStatus append_entry(SnapshotMetadata *snapshot, FileEntry *entry)
{
    if (snapshot->count >= snapshot->capacity) {
        set_last_error("too many files in snapshot");
        return CHANGE_DETECTION_TOO_MANY_FILES;
    }

    snapshot->items[snapshot->count] = *entry;
    snapshot->count++;
    return CHANGE_DETECTION_OK;
}

static int split_fields(char *line, char *fields[], int max_fields)
{
    int count = 0;
    char *cursor = line;

    while (count < max_fields) {
        char *separator = cursor;

        fields[count] = cursor;
        count++;

        while (*separator != '\0' && *separator != '|') {
            separator++;
        }

        if (*separator == '\0') {
            return count;
        }

        *separator = '\0';
        cursor = separator + 1;
    }

    while (*cursor != '\0') {
        if (*cursor == '|') {
            return -1;
        }
        cursor++;
    }

    return count;
}

static ChangeDetectionStatus parse_metadata_line(char *line, FileEntry *entry)
{
    char *fields[5];
    int field_count;

    field_count = split_fields(line, fields, 5);
    if (field_count < 0 || field_count < 3) {
        set_last_error("metadata line must be path|size|mtime");
        return CHANGE_DETECTION_PARSE_ERROR;
    }

    for (int i = 0; i < field_count; i++) {
        fields[i] = trim_whitespace(fields[i]);
    }

    memset(entry, 0, sizeof(*entry));

    if (fields[0][0] == '\0') {
        set_last_error("metadata path cannot be empty");
        return CHANGE_DETECTION_PARSE_ERROR;
    }

    if (!copy_text(entry->path, sizeof(entry->path), fields[0])) {
        set_last_error("metadata path is too long");
        return CHANGE_DETECTION_PATH_TOO_LONG;
    }

    if (!parse_uint64(fields[1], &entry->size)) {
        set_last_error("invalid metadata size");
        return CHANGE_DETECTION_PARSE_ERROR;
    }

    if (!parse_int_value(fields[2], &entry->mtime)) {
        set_last_error("invalid metadata mtime");
        return CHANGE_DETECTION_PARSE_ERROR;
    }

    if (field_count == 4 && fields[3][0] != '\0') {
        if (looks_like_hash(fields[3])) {
            if (!copy_text(entry->hash, sizeof(entry->hash), fields[3])) {
                set_last_error("metadata hash is too long");
                return CHANGE_DETECTION_PARSE_ERROR;
            }
            entry->has_hash = 1;
        } else if (parse_permissions(fields[3], &entry->permissions)) {
            entry->has_permissions = 1;
        } else {
            set_last_error("invalid metadata permissions/hash");
            return CHANGE_DETECTION_PARSE_ERROR;
        }
    }

    if (field_count == 5) {
        if (fields[3][0] != '\0') {
            if (!parse_permissions(fields[3], &entry->permissions)) {
                set_last_error("invalid metadata permissions");
                return CHANGE_DETECTION_PARSE_ERROR;
            }
            entry->has_permissions = 1;
        }

        if (fields[4][0] != '\0') {
            if (!looks_like_hash(fields[4])) {
                set_last_error("invalid metadata hash");
                return CHANGE_DETECTION_PARSE_ERROR;
            }
            if (!copy_text(entry->hash, sizeof(entry->hash), fields[4])) {
                set_last_error("metadata hash is too long");
                return CHANGE_DETECTION_PARSE_ERROR;
            }
            entry->has_hash = 1;
        }
    }

    return CHANGE_DETECTION_OK;
}

static int read_line(int fd, char *line, int line_size)
{
    int length = 0;
    char c;
    int n;

    while ((n = read(fd, &c, 1)) == 1) {
        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            break;
        }

        if (length + 1 >= line_size) {
            set_last_error("metadata line is too long");
            return -1;
        }

        line[length++] = c;
    }

    if (n < 0) {
        set_last_error("cannot read metadata file");
        return -1;
    }

    if (n == 0 && length == 0) {
        return 0;
    }

    line[length] = '\0';
    return 1;
}

static ChangeDetectionStatus load_snapshot_metadata(char *metadata_path, SnapshotMetadata *snapshot)
{
    int fd;
    int read_status;
    char line[MAX_LINE_SIZE];

    fd = open(metadata_path, O_RDONLY);
    if (fd < 0) {
        set_last_error_with_path("cannot open metadata file", metadata_path);
        return CHANGE_DETECTION_IO_ERROR;
    }

    while ((read_status = read_line(fd, line, sizeof(line))) > 0) {
        char *trimmed = trim_whitespace(line);
        FileEntry entry;
        ChangeDetectionStatus status;

        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        status = parse_metadata_line(trimmed, &entry);
        if (status != CHANGE_DETECTION_OK) {
            close(fd);
            return status;
        }

        status = append_entry(snapshot, &entry);
        if (status != CHANGE_DETECTION_OK) {
            close(fd);
            return status;
        }
    }

    close(fd);

    if (read_status < 0) {
        return CHANGE_DETECTION_IO_ERROR;
    }

    return CHANGE_DETECTION_OK;
}

static ChangeDetectionStatus create_entry_from_file(
    char *full_path,
    char *relative_path,
    cd_u64 size,
    int mtime,
    unsigned int permissions,
    FileEntry *entry
)
{
    ChangeDetectionStatus status;

    memset(entry, 0, sizeof(*entry));

    if (!copy_text(entry->path, sizeof(entry->path), relative_path)) {
        set_last_error_with_path("snapshot path is too long", relative_path);
        return CHANGE_DETECTION_PATH_TOO_LONG;
    }

    entry->size = size;
    entry->mtime = mtime;
    entry->permissions = permissions;
    entry->has_permissions = 1;

    status = compute_file_hash(full_path, entry->hash);
    if (status != CHANGE_DETECTION_OK) {
        return status;
    }

    entry->has_hash = 1;
    return CHANGE_DETECTION_OK;
}

#ifdef CHANGE_DETECTION_HOST
static int stat_type(struct stat *st)
{
    if (S_ISDIR(st->st_mode)) {
        return T_DIR;
    }

    if (S_ISREG(st->st_mode)) {
        return T_FILE;
    }

    return 0;
}

static cd_u64 stat_size(struct stat *st)
{
    return (cd_u64)st->st_size;
}

static int stat_mtime(struct stat *st)
{
    return (int)st->st_mtime;
}

static unsigned int stat_permissions(struct stat *st)
{
    return (unsigned int)(st->st_mode & 0777);
}

static ChangeDetectionStatus scan_snapshot_directory_recursive(
    char *root_path,
    char *relative_dir,
    SnapshotMetadata *snapshot
)
{
    char current_path[CHANGE_DETECTION_MAX_PATH];
    DIR *directory;
    struct dirent *de;

    if (relative_dir[0] == '\0') {
        if (!copy_text(current_path, sizeof(current_path), root_path)) {
            set_last_error("snapshot path is too long");
            return CHANGE_DETECTION_PATH_TOO_LONG;
        }
    } else if (!join_path(current_path, sizeof(current_path), root_path, relative_dir)) {
        set_last_error("snapshot path is too long");
        return CHANGE_DETECTION_PATH_TOO_LONG;
    }

    directory = opendir(current_path);
    if (directory == 0) {
        set_last_error_with_path("cannot open snapshot directory", current_path);
        return CHANGE_DETECTION_IO_ERROR;
    }

    while ((de = readdir(directory)) != 0) {
        char relative_path[CHANGE_DETECTION_MAX_PATH];
        char full_path[CHANGE_DETECTION_MAX_PATH];
        struct stat st;
        int type;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        if (relative_dir[0] == '\0' && strcmp(de->d_name, "snapshot.meta") == 0) {
            continue;
        }

        if (relative_dir[0] == '\0') {
            if (!copy_text(relative_path, sizeof(relative_path), de->d_name)) {
                closedir(directory);
                set_last_error("relative snapshot path is too long");
                return CHANGE_DETECTION_PATH_TOO_LONG;
            }
        } else if (!join_path(relative_path, sizeof(relative_path), relative_dir, de->d_name)) {
            closedir(directory);
            set_last_error("relative snapshot path is too long");
            return CHANGE_DETECTION_PATH_TOO_LONG;
        }

        if (!join_path(full_path, sizeof(full_path), root_path, relative_path)) {
            closedir(directory);
            set_last_error("snapshot path is too long");
            return CHANGE_DETECTION_PATH_TOO_LONG;
        }

        if (stat(full_path, &st) < 0) {
            closedir(directory);
            set_last_error_with_path("cannot stat snapshot file", full_path);
            return CHANGE_DETECTION_IO_ERROR;
        }

        type = stat_type(&st);
        if (type == T_DIR) {
            ChangeDetectionStatus status = scan_snapshot_directory_recursive(
                root_path,
                relative_path,
                snapshot
            );
            if (status != CHANGE_DETECTION_OK) {
                closedir(directory);
                return status;
            }
        } else if (type == T_FILE) {
            FileEntry entry;
            ChangeDetectionStatus status = create_entry_from_file(
                full_path,
                relative_path,
                stat_size(&st),
                stat_mtime(&st),
                stat_permissions(&st),
                &entry
            );

            if (status != CHANGE_DETECTION_OK) {
                closedir(directory);
                return status;
            }

            status = append_entry(snapshot, &entry);
            if (status != CHANGE_DETECTION_OK) {
                closedir(directory);
                return status;
            }
        }
    }

    closedir(directory);
    return CHANGE_DETECTION_OK;
}
#else
static void copy_dirent_name(char *dst, char *src)
{
    int i;

    for (i = 0; i < DIRSIZ && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }

    dst[i] = '\0';
}

static int is_regular_snapshot_file(struct stat *st)
{
    return st->type == T_FILE;
}

static ChangeDetectionStatus scan_snapshot_directory_recursive(
    char *root_path,
    char *relative_dir,
    SnapshotMetadata *snapshot
)
{
    char current_path[CHANGE_DETECTION_MAX_PATH];
    int fd;
    struct dirent de;

    if (relative_dir[0] == '\0') {
        if (!copy_text(current_path, sizeof(current_path), root_path)) {
            set_last_error("snapshot path is too long");
            return CHANGE_DETECTION_PATH_TOO_LONG;
        }
    } else if (!join_path(current_path, sizeof(current_path), root_path, relative_dir)) {
        set_last_error("snapshot path is too long");
        return CHANGE_DETECTION_PATH_TOO_LONG;
    }

    fd = open(current_path, O_RDONLY);
    if (fd < 0) {
        set_last_error_with_path("cannot open snapshot directory", current_path);
        return CHANGE_DETECTION_IO_ERROR;
    }

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        char name[DIRSIZ + 1];
        char relative_path[CHANGE_DETECTION_MAX_PATH];
        char full_path[CHANGE_DETECTION_MAX_PATH];
        struct stat st;

        if (de.inum == 0) {
            continue;
        }

        copy_dirent_name(name, de.name);

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        if (relative_dir[0] == '\0' && strcmp(name, "snapshot.meta") == 0) {
            continue;
        }

        if (relative_dir[0] == '\0') {
            if (!copy_text(relative_path, sizeof(relative_path), name)) {
                close(fd);
                set_last_error("relative snapshot path is too long");
                return CHANGE_DETECTION_PATH_TOO_LONG;
            }
        } else if (!join_path(relative_path, sizeof(relative_path), relative_dir, name)) {
            close(fd);
            set_last_error("relative snapshot path is too long");
            return CHANGE_DETECTION_PATH_TOO_LONG;
        }

        if (!join_path(full_path, sizeof(full_path), root_path, relative_path)) {
            close(fd);
            set_last_error("snapshot path is too long");
            return CHANGE_DETECTION_PATH_TOO_LONG;
        }

        if (stat(full_path, &st) < 0) {
            close(fd);
            set_last_error_with_path("cannot stat snapshot file", full_path);
            return CHANGE_DETECTION_IO_ERROR;
        }

        if (st.type == T_DIR) {
            ChangeDetectionStatus status = scan_snapshot_directory_recursive(
                root_path,
                relative_path,
                snapshot
            );
            if (status != CHANGE_DETECTION_OK) {
                close(fd);
                return status;
            }
        } else if (is_regular_snapshot_file(&st)) {
            FileEntry entry;
            ChangeDetectionStatus status = create_entry_from_file(
                full_path,
                relative_path,
                st.size,
                0,
                0,
                &entry
            );

            entry.has_permissions = 0;

            if (status != CHANGE_DETECTION_OK) {
                close(fd);
                return status;
            }

            status = append_entry(snapshot, &entry);
            if (status != CHANGE_DETECTION_OK) {
                close(fd);
                return status;
            }
        }
    }

    close(fd);
    return CHANGE_DETECTION_OK;
}
#endif

static ChangeDetectionStatus scan_snapshot_directory(char *snapshot_path, SnapshotMetadata *snapshot)
{
    return scan_snapshot_directory_recursive(snapshot_path, "", snapshot);
}

static ChangeDetectionStatus try_load_directory_metadata(
    char *snapshot_path,
    SnapshotMetadata *snapshot,
    int *loaded
)
{
    char metadata_path[CHANGE_DETECTION_MAX_PATH];
    ChangeDetectionStatus status;
    int fd;
    int original_count = snapshot->count;

    *loaded = 0;

    if (!join_path(metadata_path, sizeof(metadata_path), snapshot_path, "snapshot.meta")) {
        set_last_error("snapshot metadata path is too long");
        return CHANGE_DETECTION_PATH_TOO_LONG;
    }

    fd = open(metadata_path, O_RDONLY);
    if (fd < 0) {
        clear_last_error();
        return CHANGE_DETECTION_OK;
    }
    close(fd);

    status = load_snapshot_metadata(metadata_path, snapshot);
    if (status != CHANGE_DETECTION_OK) {
        snapshot->count = original_count;
        clear_last_error();
        return CHANGE_DETECTION_OK;
    }

    *loaded = 1;
    return CHANGE_DETECTION_OK;
}

static ChangeDetectionStatus load_snapshot_path(char *snapshot_path, SnapshotMetadata *snapshot)
{
    struct stat st;
    int loaded_from_metadata = 0;
    ChangeDetectionStatus status;

    if (snapshot_path == 0) {
        set_last_error("snapshot path cannot be null");
        return CHANGE_DETECTION_INVALID_ARGUMENT;
    }

    if (stat(snapshot_path, &st) < 0) {
        set_last_error_with_path("cannot stat snapshot path", snapshot_path);
        return CHANGE_DETECTION_IO_ERROR;
    }

#ifdef CHANGE_DETECTION_HOST
    if (S_ISDIR(st.st_mode)) {
#else
    if (st.type == T_DIR) {
#endif
        status = try_load_directory_metadata(snapshot_path, snapshot, &loaded_from_metadata);
        if (status != CHANGE_DETECTION_OK) {
            return status;
        }

        if (loaded_from_metadata) {
            return CHANGE_DETECTION_OK;
        }

        return scan_snapshot_directory(snapshot_path, snapshot);
    }

    return load_snapshot_metadata(snapshot_path, snapshot);
}

static FileEntry *find_entry(SnapshotMetadata *snapshot, char *path)
{
    for (int i = 0; i < snapshot->count; i++) {
        if (strcmp(snapshot->items[i].path, path) == 0) {
            return &snapshot->items[i];
        }
    }

    return 0;
}

static ChangeDetectionStatus validate_no_duplicates(SnapshotMetadata *snapshot, char *label)
{
    for (int i = 0; i < snapshot->count; i++) {
        for (int j = i + 1; j < snapshot->count; j++) {
            if (strcmp(snapshot->items[i].path, snapshot->items[j].path) == 0) {
                set_last_error_with_path("duplicate path in snapshot", label);
                return CHANGE_DETECTION_PARSE_ERROR;
            }
        }
    }

    return CHANGE_DETECTION_OK;
}

static int entries_are_modified(FileEntry *old_entry, FileEntry *new_entry)
{
    if (old_entry->has_hash && new_entry->has_hash) {
        if (strcmp(old_entry->hash, new_entry->hash) != 0) {
            return 1;
        }

        if (old_entry->size != new_entry->size) {
            return 1;
        }

        return 0;
    }

    if (old_entry->size != new_entry->size) {
        return 1;
    }

    if (old_entry->mtime != new_entry->mtime) {
        return 1;
    }

    if (
        old_entry->has_permissions &&
        new_entry->has_permissions &&
        old_entry->permissions != new_entry->permissions
    ) {
        return 1;
    }

    return 0;
}

static ChangeDetectionStatus compare_loaded_snapshots(
    SnapshotMetadata *old_snapshot,
    SnapshotMetadata *new_snapshot,
    char *old_label,
    char *new_label,
    ChangeSummary *summary
)
{
    ChangeDetectionStatus status;

    if (summary != 0) {
        summary->added = 0;
        summary->deleted = 0;
        summary->modified = 0;
    }

    status = validate_no_duplicates(old_snapshot, old_label);
    if (status != CHANGE_DETECTION_OK) {
        return status;
    }

    status = validate_no_duplicates(new_snapshot, new_label);
    if (status != CHANGE_DETECTION_OK) {
        return status;
    }

    for (int i = 0; i < new_snapshot->count; i++) {
        if (find_entry(old_snapshot, new_snapshot->items[i].path) == 0) {
            printf("Added: %s\n", new_snapshot->items[i].path);
            if (summary != 0) {
                summary->added++;
            }
        }
    }

    for (int i = 0; i < old_snapshot->count; i++) {
        if (find_entry(new_snapshot, old_snapshot->items[i].path) == 0) {
            printf("Deleted: %s\n", old_snapshot->items[i].path);
            if (summary != 0) {
                summary->deleted++;
            }
        }
    }

    for (int i = 0; i < old_snapshot->count; i++) {
        FileEntry *new_entry = find_entry(new_snapshot, old_snapshot->items[i].path);

        if (new_entry != 0 && entries_are_modified(&old_snapshot->items[i], new_entry)) {
            printf("Modified: %s\n", old_snapshot->items[i].path);
            if (summary != 0) {
                summary->modified++;
            }
        }
    }

    return CHANGE_DETECTION_OK;
}

ChangeDetectionStatus compare_snapshot_metadata(
    char *old_meta_path,
    char *new_meta_path,
    ChangeSummary *summary
)
{
    SnapshotMetadata old_snapshot;
    SnapshotMetadata new_snapshot;
    ChangeDetectionStatus status;

    clear_last_error();
    init_snapshot(&old_snapshot, old_entries, CHANGE_DETECTION_MAX_FILES);
    init_snapshot(&new_snapshot, new_entries, CHANGE_DETECTION_MAX_FILES);

    if (summary != 0) {
        summary->added = 0;
        summary->deleted = 0;
        summary->modified = 0;
    }

    if (old_meta_path == 0 || new_meta_path == 0) {
        set_last_error("metadata paths cannot be null");
        return CHANGE_DETECTION_INVALID_ARGUMENT;
    }

    status = load_snapshot_metadata(old_meta_path, &old_snapshot);
    if (status != CHANGE_DETECTION_OK) {
        return status;
    }

    status = load_snapshot_metadata(new_meta_path, &new_snapshot);
    if (status != CHANGE_DETECTION_OK) {
        return status;
    }

    return compare_loaded_snapshots(
        &old_snapshot,
        &new_snapshot,
        old_meta_path,
        new_meta_path,
        summary
    );
}

ChangeDetectionStatus compare_snapshot_paths(
    char *old_snapshot_path,
    char *new_snapshot_path,
    ChangeSummary *summary
)
{
    SnapshotMetadata old_snapshot;
    SnapshotMetadata new_snapshot;
    ChangeDetectionStatus status;

    clear_last_error();
    init_snapshot(&old_snapshot, old_entries, CHANGE_DETECTION_MAX_FILES);
    init_snapshot(&new_snapshot, new_entries, CHANGE_DETECTION_MAX_FILES);

    if (summary != 0) {
        summary->added = 0;
        summary->deleted = 0;
        summary->modified = 0;
    }

    status = load_snapshot_path(old_snapshot_path, &old_snapshot);
    if (status != CHANGE_DETECTION_OK) {
        return status;
    }

    status = load_snapshot_path(new_snapshot_path, &new_snapshot);
    if (status != CHANGE_DETECTION_OK) {
        return status;
    }

    return compare_loaded_snapshots(
        &old_snapshot,
        &new_snapshot,
        old_snapshot_path,
        new_snapshot_path,
        summary
    );
}

#ifndef CHANGE_DETECTION_LIBRARY
int main(int argc, char *argv[])
{
    ChangeSummary summary;
    ChangeDetectionStatus status;

    if (argc != 3) {
        printf("Usage: cdetect <old_snapshot> <new_snapshot>\n");
#ifdef CHANGE_DETECTION_HOST
        return 1;
#else
        exit(1);
        return 1;
#endif
    }

    status = compare_snapshot_paths(argv[1], argv[2], &summary);
    if (status != CHANGE_DETECTION_OK) {
        printf("Error: %s\n", change_detection_last_error());
#ifdef CHANGE_DETECTION_HOST
        return 1;
#else
        exit(1);
        return 1;
#endif
    }

    if (summary.added == 0 && summary.deleted == 0 && summary.modified == 0) {
        printf("No changes detected.\n");
    }

#ifdef CHANGE_DETECTION_HOST
    return 0;
#else
    exit(0);
    return 0;
#endif
}
#endif

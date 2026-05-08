#define _POSIX_C_SOURCE 200809L

#include "change_detection.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define MAX_METADATA_FIELDS 5
#define ERROR_BUFFER_SIZE 512

typedef struct {
    char *path;
    unsigned long long size;
    long long mtime;
    unsigned int permissions;
    bool has_permissions;
    char *hash;
    bool has_hash;
} FileEntry;

typedef struct {
    FileEntry *items;
    size_t count;
    size_t capacity;
} SnapshotMetadata;

static char last_error[ERROR_BUFFER_SIZE];

static void clear_last_error(void)
{
    last_error[0] = '\0';
}

static void set_last_error(const char *message)
{
    snprintf(last_error, sizeof(last_error), "%s", message);
}

static void set_last_error_with_path(const char *message, const char *path)
{
    snprintf(last_error, sizeof(last_error), "%s: %s", message, path);
}

static void set_parse_error(const char *path, size_t line_number, const char *message)
{
    snprintf(last_error, sizeof(last_error), "%s:%zu: %s", path, line_number, message);
}

const char *change_detection_last_error(void)
{
    return last_error[0] == '\0' ? "no error" : last_error;
}

static char *duplicate_string(const char *value)
{
    size_t length = strlen(value);
    char *copy = malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

static char *trim_whitespace(char *value)
{
    char *end;

    while (isspace((unsigned char)*value)) {
        value++;
    }

    if (*value == '\0') {
        return value;
    }

    end = value + strlen(value) - 1;
    while (end > value && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return value;
}

static void strip_line_ending(char *line)
{
    size_t length = strlen(line);

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        length--;
    }
}

static bool parse_unsigned_long_long(const char *text, unsigned long long *value)
{
    char *end = NULL;

    if (text == NULL || *text == '\0' || *text == '-') {
        return false;
    }

    errno = 0;
    *value = strtoull(text, &end, 10);

    return errno == 0 && end != text && *end == '\0';
}

static bool parse_long_long(const char *text, long long *value)
{
    char *end = NULL;

    if (text == NULL || *text == '\0') {
        return false;
    }

    errno = 0;
    *value = strtoll(text, &end, 10);

    return errno == 0 && end != text && *end == '\0';
}

static bool is_octal_permission_text(const char *text)
{
    size_t length = strlen(text);

    if (length == 0 || length > 4) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        if (text[i] < '0' || text[i] > '7') {
            return false;
        }
    }

    return true;
}

static bool parse_permissions(const char *text, unsigned int *permissions)
{
    char *end = NULL;
    unsigned long parsed;
    int base = is_octal_permission_text(text) ? 8 : 0;

    if (text == NULL || *text == '\0') {
        return false;
    }

    errno = 0;
    parsed = strtoul(text, &end, base);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT_MAX) {
        return false;
    }

    *permissions = (unsigned int)parsed;
    return true;
}

static bool looks_like_hash(const char *text)
{
    size_t length = strlen(text);

    if (length < 32 || length > 128) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        if (!isxdigit((unsigned char)text[i])) {
            return false;
        }
    }

    return true;
}

static void free_file_entry(FileEntry *entry)
{
    free(entry->path);
    free(entry->hash);
    memset(entry, 0, sizeof(*entry));
}

static void free_snapshot_metadata(SnapshotMetadata *snapshot)
{
    for (size_t i = 0; i < snapshot->count; i++) {
        free_file_entry(&snapshot->items[i]);
    }

    free(snapshot->items);
    memset(snapshot, 0, sizeof(*snapshot));
}

static ChangeDetectionStatus append_entry(SnapshotMetadata *snapshot, FileEntry *entry)
{
    FileEntry *resized;
    size_t new_capacity;

    if (snapshot->count == snapshot->capacity) {
        new_capacity = snapshot->capacity == 0 ? 64 : snapshot->capacity * 2;
        resized = realloc(snapshot->items, new_capacity * sizeof(*snapshot->items));
        if (resized == NULL) {
            return CHANGE_DETECTION_MEMORY_ERROR;
        }

        snapshot->items = resized;
        snapshot->capacity = new_capacity;
    }

    snapshot->items[snapshot->count] = *entry;
    snapshot->count++;
    memset(entry, 0, sizeof(*entry));

    return CHANGE_DETECTION_OK;
}

static ChangeDetectionStatus parse_metadata_line(char *line, FileEntry *entry)
{
    char *fields[MAX_METADATA_FIELDS];
    char *cursor = line;
    size_t field_count = 0;

    while (cursor != NULL && field_count < MAX_METADATA_FIELDS) {
        char *separator = strchr(cursor, '|');

        fields[field_count] = cursor;
        field_count++;

        if (separator == NULL) {
            cursor = NULL;
        } else {
            *separator = '\0';
            cursor = separator + 1;
        }
    }

    if (cursor != NULL) {
        set_last_error("too many metadata fields");
        return CHANGE_DETECTION_PARSE_ERROR;
    }

    if (field_count < 3) {
        set_last_error("metadata line must contain at least path, size, and mtime");
        return CHANGE_DETECTION_PARSE_ERROR;
    }

    for (size_t i = 0; i < field_count; i++) {
        fields[i] = trim_whitespace(fields[i]);
    }

    if (fields[0][0] == '\0') {
        set_last_error("path cannot be empty");
        return CHANGE_DETECTION_PARSE_ERROR;
    }

    memset(entry, 0, sizeof(*entry));
    entry->path = duplicate_string(fields[0]);
    if (entry->path == NULL) {
        return CHANGE_DETECTION_MEMORY_ERROR;
    }

    if (!parse_unsigned_long_long(fields[1], &entry->size)) {
        free_file_entry(entry);
        set_last_error("invalid file size");
        return CHANGE_DETECTION_PARSE_ERROR;
    }

    if (!parse_long_long(fields[2], &entry->mtime)) {
        free_file_entry(entry);
        set_last_error("invalid modification time");
        return CHANGE_DETECTION_PARSE_ERROR;
    }

    if (field_count == 4 && fields[3][0] != '\0') {
        if (looks_like_hash(fields[3])) {
            entry->hash = duplicate_string(fields[3]);
            if (entry->hash == NULL) {
                free_file_entry(entry);
                return CHANGE_DETECTION_MEMORY_ERROR;
            }
            entry->has_hash = true;
        } else if (parse_permissions(fields[3], &entry->permissions)) {
            entry->has_permissions = true;
        } else {
            free_file_entry(entry);
            set_last_error("invalid permissions or hash field");
            return CHANGE_DETECTION_PARSE_ERROR;
        }
    }

    if (field_count == 5) {
        if (fields[3][0] != '\0') {
            if (!parse_permissions(fields[3], &entry->permissions)) {
                free_file_entry(entry);
                set_last_error("invalid permissions");
                return CHANGE_DETECTION_PARSE_ERROR;
            }
            entry->has_permissions = true;
        }

        if (fields[4][0] != '\0') {
            entry->hash = duplicate_string(fields[4]);
            if (entry->hash == NULL) {
                free_file_entry(entry);
                return CHANGE_DETECTION_MEMORY_ERROR;
            }
            entry->has_hash = true;
        }
    }

    return CHANGE_DETECTION_OK;
}

static ChangeDetectionStatus load_snapshot_metadata(
    const char *metadata_path,
    SnapshotMetadata *snapshot
)
{
    FILE *file;
    char *line = NULL;
    size_t line_capacity = 0;
    size_t line_number = 0;
    ssize_t read_count;
    ChangeDetectionStatus status = CHANGE_DETECTION_OK;

    file = fopen(metadata_path, "r");
    if (file == NULL) {
        set_last_error_with_path("cannot open metadata file", metadata_path);
        return CHANGE_DETECTION_IO_ERROR;
    }

    while ((read_count = getline(&line, &line_capacity, file)) != -1) {
        char *trimmed;
        FileEntry entry;

        (void)read_count;
        line_number++;
        strip_line_ending(line);
        trimmed = trim_whitespace(line);

        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        status = parse_metadata_line(trimmed, &entry);
        if (status != CHANGE_DETECTION_OK) {
            char detail[ERROR_BUFFER_SIZE];
            snprintf(detail, sizeof(detail), "%s", change_detection_last_error());
            set_parse_error(metadata_path, line_number, detail);
            break;
        }

        status = append_entry(snapshot, &entry);
        if (status != CHANGE_DETECTION_OK) {
            free_file_entry(&entry);
            set_last_error("out of memory while loading metadata");
            break;
        }
    }

    if (ferror(file) && status == CHANGE_DETECTION_OK) {
        set_last_error_with_path("error while reading metadata file", metadata_path);
        status = CHANGE_DETECTION_IO_ERROR;
    }

    free(line);
    fclose(file);

    return status;
}

static int compare_entries_by_path(const void *left, const void *right)
{
    const FileEntry *left_entry = left;
    const FileEntry *right_entry = right;

    return strcmp(left_entry->path, right_entry->path);
}

static ChangeDetectionStatus sort_and_validate_snapshot(
    SnapshotMetadata *snapshot,
    const char *metadata_path
)
{
    if (snapshot->count == 0) {
        return CHANGE_DETECTION_OK;
    }

    qsort(snapshot->items, snapshot->count, sizeof(*snapshot->items), compare_entries_by_path);

    for (size_t i = 1; i < snapshot->count; i++) {
        if (strcmp(snapshot->items[i - 1].path, snapshot->items[i].path) == 0) {
            snprintf(
                last_error,
                sizeof(last_error),
                "%s: duplicate path in metadata: %s",
                metadata_path,
                snapshot->items[i].path
            );
            return CHANGE_DETECTION_PARSE_ERROR;
        }
    }

    return CHANGE_DETECTION_OK;
}

static const FileEntry *find_entry(const SnapshotMetadata *snapshot, const char *path)
{
    size_t left = 0;
    size_t right = snapshot->count;

    while (left < right) {
        size_t middle = left + (right - left) / 2;
        int comparison = strcmp(snapshot->items[middle].path, path);

        if (comparison == 0) {
            return &snapshot->items[middle];
        }

        if (comparison < 0) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    return NULL;
}

static bool entries_are_modified(const FileEntry *old_entry, const FileEntry *new_entry)
{
    if (old_entry->size != new_entry->size) {
        return true;
    }

    if (old_entry->mtime != new_entry->mtime) {
        return true;
    }

    if (
        old_entry->has_permissions &&
        new_entry->has_permissions &&
        old_entry->permissions != new_entry->permissions
    ) {
        return true;
    }

    if (
        old_entry->has_hash &&
        new_entry->has_hash &&
        strcmp(old_entry->hash, new_entry->hash) != 0
    ) {
        return true;
    }

    return false;
}

ChangeDetectionStatus compare_snapshot_metadata(
    const char *old_meta_path,
    const char *new_meta_path,
    FILE *output,
    ChangeSummary *summary
)
{
    SnapshotMetadata old_snapshot = {0};
    SnapshotMetadata new_snapshot = {0};
    ChangeSummary local_summary = {0};
    ChangeDetectionStatus status;

    clear_last_error();

    if (summary != NULL) {
        *summary = local_summary;
    }

    if (old_meta_path == NULL || new_meta_path == NULL) {
        set_last_error("metadata paths cannot be NULL");
        return CHANGE_DETECTION_INVALID_ARGUMENT;
    }

    status = load_snapshot_metadata(old_meta_path, &old_snapshot);
    if (status != CHANGE_DETECTION_OK) {
        goto cleanup;
    }

    status = load_snapshot_metadata(new_meta_path, &new_snapshot);
    if (status != CHANGE_DETECTION_OK) {
        goto cleanup;
    }

    status = sort_and_validate_snapshot(&old_snapshot, old_meta_path);
    if (status != CHANGE_DETECTION_OK) {
        goto cleanup;
    }

    status = sort_and_validate_snapshot(&new_snapshot, new_meta_path);
    if (status != CHANGE_DETECTION_OK) {
        goto cleanup;
    }

    for (size_t i = 0; i < new_snapshot.count; i++) {
        if (find_entry(&old_snapshot, new_snapshot.items[i].path) == NULL) {
            if (output != NULL) {
                fprintf(output, "Added: %s\n", new_snapshot.items[i].path);
            }
            local_summary.added++;
        }
    }

    for (size_t i = 0; i < old_snapshot.count; i++) {
        if (find_entry(&new_snapshot, old_snapshot.items[i].path) == NULL) {
            if (output != NULL) {
                fprintf(output, "Deleted: %s\n", old_snapshot.items[i].path);
            }
            local_summary.deleted++;
        }
    }

    for (size_t i = 0; i < old_snapshot.count; i++) {
        const FileEntry *new_entry = find_entry(&new_snapshot, old_snapshot.items[i].path);

        if (new_entry != NULL && entries_are_modified(&old_snapshot.items[i], new_entry)) {
            if (output != NULL) {
                fprintf(output, "Modified: %s\n", old_snapshot.items[i].path);
            }
            local_summary.modified++;
        }
    }

    if (summary != NULL) {
        *summary = local_summary;
    }

cleanup:
    free_snapshot_metadata(&old_snapshot);
    free_snapshot_metadata(&new_snapshot);
    return status;
}

#ifdef CHANGE_DETECTION_STANDALONE
int main(int argc, char **argv)
{
    ChangeSummary summary = {0};
    ChangeDetectionStatus status;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <old_snapshot.meta> <new_snapshot.meta>\n", argv[0]);
        return EXIT_FAILURE;
    }

    status = compare_snapshot_metadata(argv[1], argv[2], stdout, &summary);
    if (status != CHANGE_DETECTION_OK) {
        fprintf(stderr, "Error: %s\n", change_detection_last_error());
        return EXIT_FAILURE;
    }

    if (summary.added == 0 && summary.deleted == 0 && summary.modified == 0) {
        printf("No changes detected.\n");
    }

    return EXIT_SUCCESS;
}
#endif

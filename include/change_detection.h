#ifndef CHANGE_DETECTION_H
#define CHANGE_DETECTION_H

/*
 * Part 4: Change Detection / Comparison Engine
 *
 * xv6-friendly public interface:
 * - no stdio.h
 * - no FILE *
 * - no malloc-facing API
 *
 * Supported metadata line format, when a real metadata file exists:
 *   path|size|mtime|permissions|sha256
 *
 * Minimum supported metadata format:
 *   path|size|mtime
 *
 * permissions and sha256 are optional. If the snapshot manager creates only a
 * status-style snapshot.meta, compare_snapshot_paths() can compare the files
 * inside the snapshot directories directly.
 */

#define CHANGE_DETECTION_MAX_FILES 1000
#define CHANGE_DETECTION_MAX_PATH 128
#define CHANGE_DETECTION_HASH_SIZE 65

typedef struct {
    int added;
    int deleted;
    int modified;
} ChangeSummary;

typedef enum {
    CHANGE_DETECTION_OK = 0,
    CHANGE_DETECTION_INVALID_ARGUMENT = 1,
    CHANGE_DETECTION_IO_ERROR = 2,
    CHANGE_DETECTION_PARSE_ERROR = 3,
    CHANGE_DETECTION_TOO_MANY_FILES = 4,
    CHANGE_DETECTION_PATH_TOO_LONG = 5
} ChangeDetectionStatus;

ChangeDetectionStatus compare_snapshot_metadata(
    char *old_meta_path,
    char *new_meta_path,
    ChangeSummary *summary
);

ChangeDetectionStatus compare_snapshot_paths(
    char *old_snapshot_path,
    char *new_snapshot_path,
    ChangeSummary *summary
);

char *change_detection_last_error(void);

#endif

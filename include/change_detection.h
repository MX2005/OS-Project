#ifndef CHANGE_DETECTION_H
#define CHANGE_DETECTION_H

#include <stddef.h>
#include <stdio.h>

/*
 * Part 4: Change Detection / Comparison Engine
 *
 * Expected metadata line format:
 *   path|size|mtime|permissions|sha256
 *
 * Minimum supported format:
 *   path|size|mtime
 *
 * permissions and sha256 are optional. Blank lines and lines starting with '#'
 * are ignored.
 */

typedef struct {
    size_t added;
    size_t deleted;
    size_t modified;
} ChangeSummary;

typedef enum {
    CHANGE_DETECTION_OK = 0,
    CHANGE_DETECTION_INVALID_ARGUMENT = 1,
    CHANGE_DETECTION_IO_ERROR = 2,
    CHANGE_DETECTION_PARSE_ERROR = 3,
    CHANGE_DETECTION_MEMORY_ERROR = 4
} ChangeDetectionStatus;

/*
 * Compare two snapshot metadata files.
 *
 * old_meta_path: metadata file from the older snapshot
 * new_meta_path: metadata file from the newer/current snapshot
 * output: where to print "Added:", "Deleted:", and "Modified:" lines.
 *         Pass NULL if you only want the summary counts.
 * summary: optional output counts. Pass NULL if counts are not needed.
 *
 * Returns CHANGE_DETECTION_OK on success, otherwise an error status.
 */
ChangeDetectionStatus compare_snapshot_metadata(
    const char *old_meta_path,
    const char *new_meta_path,
    FILE *output,
    ChangeSummary *summary
);

const char *change_detection_last_error(void);

#endif

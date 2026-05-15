# 🗂️ XV6 File System — Snapshot & Restore Lab

> **Course Lab | Operating Systems | Section 5B**
> Implementing snapshot creation, change detection, directory restore, and a **bonus snapshot tree visualizer** on a custom xv6 RISC-V kernel.

---

## 📋 Overview

This lab extends the xv6 operating system (RISC-V) with a full **snapshot and restore** subsystem for the file system. The implementation adds several new user-space commands that work together to capture filesystem state, detect changes between snapshots, restore a directory to a prior state, and **visually explore snapshot contents as a tree**.

The kernel runs inside QEMU with a virtual block device (`fs.img`) and supports multi-core execution via SMP.

---

## ✨ Features Implemented

### Core Commands

| Command | Description |
| :--- | :--- |
| `scanner <dir>` | Recursively lists all files in a directory with path, size, type, and inode number |
| `snapshot <name>` | Copies all files from a target directory into `snapshots/<name>/` and writes a metadata file |
| `mksnap <name>` | Initializes a named snapshot storage directory |
| `cdetect <snap1> <snap2>` | Compares two snapshots and reports Added, Deleted, and Modified files |
| `restore <snap> <dir>` | Restores a directory to match a given snapshot, deleting files not present in the snapshot |

### 🌟 Bonus Feature — `snapshot_tree`

| Command | Description |
| :--- | :--- |
| `snapshot_tree <snap>` | Displays the full contents of a snapshot directory as a formatted file tree |

---

## 🚀 How to Run

### 1. Build the Kernel

```bash
make
```

### 2. Launch QEMU

```bash
qemu-system-riscv64 \
  -machine virt \
  -bios none \
  -kernel kernel/kernel \
  -m 128M \
  -smp 3 \
  -nographic \
  -global virtio-mmio.force-legacy=false \
  -drive file=fs.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
```

### 3. Example Workflow

```sh
# Create a directory with files
$ mkdir dir
$ echo A > dir/a.txt
$ echo B > dir/b.txt

# Scan the directory
$ scanner dir
Path: dir/a.txt | Size: 2 | Type: 2 | Inode: 38
Path: dir/b.txt | Size: 2 | Type: 2 | Inode: 39
Scan completed.

# Take first snapshot
$ snapshot snap1
=== Snapshot Creation Started ===
Copied: dir/a.txt -> snapshots/snap1/a.txt
Copied: dir/b.txt -> snapshots/snap1/b.txt
=== Snapshot Created: snap1 ===

$ mksnap snap1
Snapshot storage ready: snap1

# Modify the directory
$ echo NEW >> dir/a.txt
$ rm dir/b.txt
$ echo C > dir/c.txt

# Take second snapshot
$ snapshot snap2
=== Snapshot Creation Started ===
Copied: dir/a.txt -> snapshots/snap2/a.txt
Copied: dir/c.txt -> snapshots/snap2/c.txt
=== Snapshot Created: snap2 ===

$ mksnap snap2
Snapshot storage ready: snap2

# Detect changes between snapshots
$ cdetect snapshots/snap1 snapshots/snap2
Added:    c.txt
Deleted:  b.txt
Modified: a.txt

# Restore directory to snap1
$ restore snapshots/snap1 dir
restore: === starting restore ===
restore: snapshot : snapshots/snap1
restore: target   : dir
restore: ---
restore: OK       a.txt  (2 bytes)
restore: OK       b.txt  (2 bytes)
restore: DELETED  c.txt  (not in snapshot)
restore: ---
restore: === done ===
```

---

## 🌲 Bonus: Snapshot Tree Visualizer

The `snapshot_tree` command renders the full contents of any snapshot as a human-readable directory tree, making it easy to inspect what was captured at a glance.

### Usage

```bash
$ snapshot_tree snapshots/snap1
```

### Expected Output

```
snapshots/snap1
|-- a.txt
|-- b.txt
|-- dir
    |-- c.txt
```

This is especially useful when snapshots contain nested subdirectories, allowing you to verify the exact structure captured at the time of the snapshot without scanning each file individually.

---

## 🔍 Enhanced Snapshot Metadata

Each snapshot directory contains a `snapshot.meta` file. In addition to the standard fields, the metadata now includes the **total size** of all files captured in the snapshot:

```
Snapshot Name: snap1
Status: CREATED
Managed By: Snapshot Storage Manager
Total Size: 40 bytes
```

This makes it easy to track storage usage across multiple snapshots at a glance.

---

## 🧪 Test Results

The full implementation was verified with the following end-to-end scenario:

| Step | Action | Result |
| :--- | :--- | :--- |
| 1 | Created `dir/` with `a.txt` (2B) and `b.txt` (2B) | ✅ Files written |
| 2 | `snapshot snap1` | ✅ Both files copied, metadata written |
| 3 | Appended to `a.txt`, deleted `b.txt`, created `c.txt` | ✅ Directory modified |
| 4 | `snapshot snap2` | ✅ New state captured |
| 5 | `cdetect snap1 snap2` | ✅ Added: c.txt, Deleted: b.txt, Modified: a.txt |
| 6 | `restore snap1 dir` | ✅ a.txt restored (2B), b.txt restored, c.txt deleted |
| 7 | `snapshot_tree snap1` | ✅ Tree displayed correctly with nested structure |
| 8 | `cat snap1/snapshot.meta` | ✅ Total Size shown as 40 bytes |

---

## 👥 Team 5 Members

| Name | Student ID | Email | Section |
| :--- | :--- | :--- | :--- |
| **Mostafa Abd Elhamied Ismael** | `231000842` | [M.AbdElhamied2342@nu.edu.eg](mailto:M.AbdElhamied2342@nu.edu.eg) | 5B |
| **Ziad Abdelwahab** | `231000621` | [Z.Khaled2321@nu.edu.eg](mailto:Z.Khaled2321@nu.edu.eg) | 5B |
| **Mahmoud Khaled** | `231000616` | [M.Khaled2316@nu.edu.eg](mailto:M.Khaled2316@nu.edu.eg) | 5B |
| **Ahmed Wael Ahmed** | `231001030` | [a.wael2330@nu.edu.eg](mailto:a.wael2330@nu.edu.eg) | 5B |
| **Yousef Kandil** | `231000562` | [Y.Mohamed2362@nu.edu.eg](mailto:Y.Mohamed2362@nu.edu.eg) | 5B |

---

## 🏫 Academic Info

- **University**: Nile University
- **Course**: Operating Systems
- **Lab**: XV6 RISC-V File System Extension
- **Section**: 5B
- **Date**: May 2025

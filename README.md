# 🗂️ XV6 File System — Snapshot & Restore Lab

> **Course Lab | Operating Systems | Section 5B**
> Implementing snapshot creation, change detection, and directory restore on a custom xv6 RISC-V kernel.

---

## 📋 Overview

This lab extends the xv6 operating system (RISC-V) with a **snapshot and restore** subsystem for the file system. The implementation adds four new user-space commands that work together to capture filesystem state, detect changes between snapshots, and restore a directory to a prior state.

The kernel is run inside QEMU with a virtual block device (`fs.img`) and supports multi-core execution via SMP.

---

## ✨ Features Implemented

| Command | Description |
| :--- | :--- |
| `scanner <dir>` | Recursively lists all files in a directory with path, size, type, and inode number |
| `snapshot <name>` | Copies all files from a target directory into `snapshots/<name>/` with a metadata file |
| `mksnap <name>` | Initializes a named snapshot storage directory |
| `cdetect <snap1> <snap2>` | Compares two snapshots and reports Added, Deleted, and Modified files |
| `restore <snap> <dir>` | Restores a directory to match a given snapshot, deleting files not in the snapshot |

---

## 🚀 How to Run

### 1. Build and Launch the Kernel

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

### 2. Example Workflow

```sh
# Create a directory with files
$ mkdir dir
$ echo A > dir/a.txt
$ echo B > dir/b.txt

# Scan the directory
$ scanner dir
Path: dir/a.txt | Size: 2 | Type: 2 | Inode: 38
Path: dir/b.txt | Size: 2 | Type: 2 | Inode: 39

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

## 🔍 Snapshot Metadata

Each snapshot directory contains a `snapshot.meta` file:

```
Snapshot Name: snap2
Status: CREATED
Managed By: Snapshot Storage Manager
```

---

## 🧪 Test Results

The implementation was verified with the following scenario:

1. **Initial state** — `dir/` contains `a.txt` (2 bytes) and `b.txt` (2 bytes)
2. **snap1** — captures both files
3. **Modifications** — `a.txt` appended to (now 4 bytes), `b.txt` deleted, `c.txt` created
4. **snap2** — captures new state (`a.txt` + `c.txt`)
5. **cdetect** — correctly identifies all three change types
6. **restore to snap1** — `a.txt` restored to original 2-byte content, `b.txt` restored, `c.txt` deleted

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

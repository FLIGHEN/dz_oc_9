#!/usr/bin/env bash

set -euo pipefail

IMG=ext2.img
MNT=ext2_mnt
LOOP=""
HELLO_PATH="$MNT/dir1/hello.txt"
BIG_PATH="$MNT/dir2/big.bin"
SPARSE_PATH="$MNT/dir3/sub/sparse.bin"

hash_inode() {
    local source=$1
    local inode=$2

    ./inode_data "$source" "$inode" | sha512sum | awk '{print $1}'
}

hash_file() {
    local path=$1

    sha512sum "$path" | awk '{print $1}'
}

require_dir_entry() {
    local source=$1
    local dir_inode=$2
    local expected_inode=$3
    local expected_type=$4
    local expected_name=$5

    if ! ./inode_data "$source" "$dir_inode" |
        ./dir_parse |
        awk -v ino="$expected_inode" -v type="$expected_type" -v name="$expected_name" \
            '$1 == ino && $2 == type && $3 == name { found = 1 } END { exit found ? 0 : 1 }'; then
        echo "missing directory entry: dir_inode=$dir_inode inode=$expected_inode type=$expected_type name=$expected_name" >&2
        return 1
    fi
}

check_inode_hash() {
    local source=$1
    local inode=$2
    local expected=$3
    local label=$4

    local actual
    actual=$(hash_inode "$source" "$inode")

    if [[ "$actual" != "$expected" ]]; then
        echo "checksum mismatch for $label" >&2
        echo "expected: $expected" >&2
        echo "actual:   $actual" >&2
        return 1
    fi
}

cleanup() {
    if [[ -n "$LOOP" ]]; then
        if losetup "$LOOP" >/dev/null 2>&1; then
            sudo losetup -d "$LOOP" || true
        fi
    fi

    if mountpoint -q "$MNT"; then
        sudo umount "$MNT" || true
    fi
}

trap cleanup EXIT

if mountpoint -q "$MNT"; then
    echo "[0] unmount previous mount"
    sudo umount "$MNT"
fi

echo "[1] create image"
truncate --size 512M "$IMG"

echo "[2] create ext2"
mkfs.ext2 -F -b 2048 "$IMG"

mkdir -p "$MNT"

echo "[3] mount"
sudo mount -t ext2 "$IMG" "$MNT"
sudo chown "$(id -u):$(id -g)" "$MNT"

echo "[4] create dirs"
mkdir -p "$MNT/dir1"
mkdir -p "$MNT/dir2"
mkdir -p "$MNT/dir3/sub"

echo "[5] create files"
echo "hello ext2" > "$HELLO_PATH"
dd if=/dev/urandom of="$BIG_PATH" bs=2048 count=700 status=none
truncate -s 5G "$SPARSE_PATH"
printf 'sparse-data\n' | dd of="$SPARSE_PATH" bs=1 seek=0 conv=notrunc status=none

sync

echo "[6] save inode numbers"
ROOT_INO=2
HELLO_INO=$(stat -c '%i' "$HELLO_PATH")
BIG_INO=$(stat -c '%i' "$BIG_PATH")
SPARSE_INO=$(stat -c '%i' "$SPARSE_PATH")
DIR1_INO=$(stat -c '%i' "$MNT/dir1")
DIR2_INO=$(stat -c '%i' "$MNT/dir2")
DIR3_INO=$(stat -c '%i' "$MNT/dir3")
SUB_INO=$(stat -c '%i' "$MNT/dir3/sub")

echo root="$ROOT_INO"
echo hello="$HELLO_INO"
echo big="$BIG_INO"
echo sparse="$SPARSE_INO"
echo dir1="$DIR1_INO"
echo dir2="$DIR2_INO"
echo dir3="$DIR3_INO"
echo sub="$SUB_INO"

echo "[7] save checksums"
sha512sum "$HELLO_PATH" > hello.sha512
sha512sum "$BIG_PATH" > big.sha512
sha512sum "$SPARSE_PATH" > sparse.sha512
HELLO_HASH=$(hash_file "$HELLO_PATH")
BIG_HASH=$(hash_file "$BIG_PATH")
SPARSE_HASH=$(hash_file "$SPARSE_PATH")

echo "[8] umount"
sudo umount "$MNT"

echo "[9] verify inode_info"
./inode_info "$IMG" "$HELLO_INO"
./inode_info "$IMG" "$BIG_INO"
./inode_info "$IMG" "$SPARSE_INO"

echo "[10] verify inode_data checksums"
check_inode_hash "$IMG" "$HELLO_INO" "$HELLO_HASH" "hello.txt from image"
check_inode_hash "$IMG" "$BIG_INO" "$BIG_HASH" "big.bin from image"
check_inode_hash "$IMG" "$SPARSE_INO" "$SPARSE_HASH" "sparse.bin from image"

echo "[11] verify directory parse"
require_dir_entry "$IMG" "$ROOT_INO" "$DIR1_INO" directory dir1
require_dir_entry "$IMG" "$ROOT_INO" "$DIR2_INO" directory dir2
require_dir_entry "$IMG" "$ROOT_INO" "$DIR3_INO" directory dir3
require_dir_entry "$IMG" "$DIR1_INO" "$ROOT_INO" directory ..
require_dir_entry "$IMG" "$DIR1_INO" "$HELLO_INO" regular hello.txt
require_dir_entry "$IMG" "$DIR2_INO" "$BIG_INO" regular big.bin
require_dir_entry "$IMG" "$DIR3_INO" "$SUB_INO" directory sub
require_dir_entry "$IMG" "$SUB_INO" "$SPARSE_INO" regular sparse.bin

echo "[12] loop device"
LOOP=$(sudo losetup -f)
sudo losetup "$LOOP" "$IMG"
sudo chown "$(id -u):$(id -g)" "$LOOP"

losetup -a
lsblk -o name,size,fstype

./inode_info "$LOOP" "$HELLO_INO"
./inode_info "$LOOP" "$BIG_INO"
./inode_info "$LOOP" "$SPARSE_INO"
check_inode_hash "$LOOP" "$HELLO_INO" "$HELLO_HASH" "hello.txt from loop"
check_inode_hash "$LOOP" "$BIG_INO" "$BIG_HASH" "big.bin from loop"
check_inode_hash "$LOOP" "$SPARSE_INO" "$SPARSE_HASH" "sparse.bin from loop"
require_dir_entry "$LOOP" "$ROOT_INO" "$DIR1_INO" directory dir1
require_dir_entry "$LOOP" "$ROOT_INO" "$DIR2_INO" directory dir2
require_dir_entry "$LOOP" "$ROOT_INO" "$DIR3_INO" directory dir3
require_dir_entry "$LOOP" "$DIR1_INO" "$HELLO_INO" regular hello.txt
require_dir_entry "$LOOP" "$DIR2_INO" "$BIG_INO" regular big.bin
require_dir_entry "$LOOP" "$DIR3_INO" "$SUB_INO" directory sub
require_dir_entry "$LOOP" "$SUB_INO" "$SPARSE_INO" regular sparse.bin

sudo losetup -d "$LOOP"
LOOP=""

echo "DONE"

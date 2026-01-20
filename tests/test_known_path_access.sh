#!/bin/bash
# Test Known Path Access - Tests what Valkyrie-FS currently supports
# This tests direct file access when you know the exact S3 key/path

set -e
set -u

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

TEST_BUCKET="${TEST_BUCKET:-valkyrie-test-bucket}"
TEST_REGION="${TEST_REGION:-us-east-1}"
MOUNT_POINT="${MOUNT_POINT:-/tmp/valkyrie-mount}"
TEST_PREFIX="known-path-test-$(date +%s)"
VALKYRIE_BIN="./build/bin/valkyrie"
TEMP_DIR="/tmp/valkyrie-known-path-$$"
VALKYRIE_PID=""

cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"

    if mount | grep -q "$MOUNT_POINT"; then
        umount "$MOUNT_POINT" 2>/dev/null || sudo -A umount "$MOUNT_POINT" 2>/dev/null || true
        sleep 1
    fi

    if [ -n "$VALKYRIE_PID" ] && kill -0 "$VALKYRIE_PID" 2>/dev/null; then
        kill "$VALKYRIE_PID" 2>/dev/null || true
        sleep 1
        kill -9 "$VALKYRIE_PID" 2>/dev/null || true
    fi

    [ -d "$MOUNT_POINT" ] && rmdir "$MOUNT_POINT" 2>/dev/null || true
    [ -d "$TEMP_DIR" ] && rm -rf "$TEMP_DIR"

    echo "Removing test files from S3..."
    aws s3 rm "s3://${TEST_BUCKET}/${TEST_PREFIX}/testfile.dat" \
        --region "$TEST_REGION" 2>/dev/null || true

    echo -e "${GREEN}Cleanup complete${NC}"
}

trap cleanup EXIT INT TERM

print_section() {
    echo ""
    echo "=========================================="
    echo "$1"
    echo "=========================================="
}

print_result() {
    if [ "$1" -eq 0 ]; then
        echo -e "${GREEN}✓ PASS${NC}: $2"
    else
        echo -e "${RED}✗ FAIL${NC}: $2"
        exit 1
    fi
}

print_section "Setup: Creating Test File"

mkdir -p "$TEMP_DIR"
mkdir -p "$MOUNT_POINT"

# Create a 10MB test file
echo "Generating testfile.dat (10MB)..."
dd if=/dev/urandom of="$TEMP_DIR/testfile.dat" bs=1m count=10 2>/dev/null

# Calculate MD5
echo "Calculating MD5..."
if command -v md5sum &> /dev/null; then
    expected_md5=$(md5sum "$TEMP_DIR/testfile.dat" | awk '{print $1}')
else
    expected_md5=$(md5 -q "$TEMP_DIR/testfile.dat")
fi
echo "Expected MD5: $expected_md5"

# Upload to S3
s3_path="s3://${TEST_BUCKET}/${TEST_PREFIX}/testfile.dat"
echo "Uploading to S3: $s3_path"
aws s3 cp "$TEMP_DIR/testfile.dat" "$s3_path" --region "$TEST_REGION"
print_result 0 "Test file uploaded to S3"

print_section "Starting Valkyrie-FS"

echo "Starting Valkyrie-FS with:"
echo "  Bucket: $TEST_BUCKET"
echo "  Region: $TEST_REGION"
echo "  Mount: $MOUNT_POINT"
echo "  Prefix: $TEST_PREFIX"

# Use -E to preserve environment variables (AWS_PROFILE, AWS_REGION, etc.)
sudo -A -E "$VALKYRIE_BIN" \
    --bucket "$TEST_BUCKET" \
    --region "$TEST_REGION" \
    --mount "$MOUNT_POINT" \
    --s3-prefix "$TEST_PREFIX" \
    --cache-size 512M \
    --workers 4 \
    > "$TEMP_DIR/valkyrie.log" 2>&1 &

VALKYRIE_PID=$!
echo "Valkyrie-FS started (PID: $VALKYRIE_PID)"

# Wait for mount
echo "Waiting for filesystem to mount..."
for i in {1..30}; do
    if mount | grep -q "$MOUNT_POINT"; then
        echo -e "${GREEN}✓${NC} Filesystem mounted"
        break
    fi
    if ! kill -0 "$VALKYRIE_PID" 2>/dev/null; then
        echo -e "${RED}Error: Valkyrie-FS process died${NC}"
        cat "$TEMP_DIR/valkyrie.log"
        exit 1
    fi
    sleep 1
done

if ! mount | grep -q "$MOUNT_POINT"; then
    echo -e "${RED}Error: Filesystem failed to mount after 30 seconds${NC}"
    cat "$TEMP_DIR/valkyrie.log"
    exit 1
fi

print_section "Test 1: Direct File Access (Known Path)"

echo "Attempting to read file using known path: $MOUNT_POINT/testfile.dat"
mounted_file="$MOUNT_POINT/testfile.dat"

# Check if file is accessible by direct path
if [ -f "$mounted_file" ]; then
    print_result 1 "File should not be stat-able before open (current implementation limitation)"
fi

# Try to read the file directly
echo "Reading file via cat..."
if cat "$mounted_file" > "$TEMP_DIR/read_output.dat" 2>/dev/null; then
    print_result 0 "File successfully read via direct path access"
else
    print_result 1 "Failed to read file via direct path"
fi

print_section "Test 2: MD5 Verification"

echo "Calculating MD5 of read data..."
if command -v md5sum &> /dev/null; then
    actual_md5=$(md5sum "$TEMP_DIR/read_output.dat" | awk '{print $1}')
else
    actual_md5=$(md5 -q "$TEMP_DIR/read_output.dat")
fi

echo "Expected MD5: $expected_md5"
echo "Actual MD5:   $actual_md5"

if [ "$expected_md5" = "$actual_md5" ]; then
    print_result 0 "MD5 checksum matches - data integrity verified"
else
    print_result 1 "MD5 checksum mismatch"
fi

print_section "Test 3: Directory Listing After File Access"

echo "Now that file has been opened, check if it appears in directory listing..."
if ls -la "$MOUNT_POINT" | grep -q "testfile.dat"; then
    print_result 0 "File appears in directory listing after being accessed"
else
    echo -e "${YELLOW}⚠${NC} File does not appear in directory listing"
    echo "This is a known limitation of the current implementation"
fi

print_section "Test Summary"

echo -e "${GREEN}Known path access tests passed!${NC}"
echo ""
echo "Current Implementation Support:"
echo "  ✓ Direct file access by known path"
echo "  ✓ Data integrity (MD5 verification)"
echo "  ✓ Read operations"
echo ""
echo "Known Limitations:"
echo "  ⚠ No S3 ListObjects integration (can't discover files via ls)"
echo "  ⚠ getattr returns ENOENT for files not in metadata cache"
echo "  ⚠ Files only appear in directory listing after being opened"
echo ""
echo "Valkyrie-FS log available at: $TEMP_DIR/valkyrie.log"

exit 0

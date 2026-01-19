#!/bin/bash
# End-to-End Test for Valkyrie-FS
# Tests the complete system: mount, S3 upload, read, MD5 verification

set -e  # Exit on error
set -u  # Exit on undefined variable

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test configuration
TEST_BUCKET="${TEST_BUCKET:-valkyrie-test-bucket}"
TEST_REGION="${TEST_REGION:-us-west-2}"
MOUNT_POINT="${MOUNT_POINT:-/tmp/valkyrie-mount}"
TEST_PREFIX="e2e-test-$(date +%s)"
VALKYRIE_BIN="./build/bin/valkyrie"
TEMP_DIR="/tmp/valkyrie-e2e-$$"
VALKYRIE_PID=""

# Test files
TEST_FILE_SIZE=10485760  # 10MB
NUM_TEST_FILES=3

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"

    # Unmount filesystem
    if mount | grep -q "$MOUNT_POINT"; then
        echo "Unmounting $MOUNT_POINT..."
        umount "$MOUNT_POINT" 2>/dev/null || sudo umount "$MOUNT_POINT" 2>/dev/null || true
        sleep 1
    fi

    # Kill Valkyrie-FS process
    if [ -n "$VALKYRIE_PID" ] && kill -0 "$VALKYRIE_PID" 2>/dev/null; then
        echo "Stopping Valkyrie-FS (PID: $VALKYRIE_PID)..."
        kill "$VALKYRIE_PID" 2>/dev/null || true
        sleep 1
        kill -9 "$VALKYRIE_PID" 2>/dev/null || true
    fi

    # Remove mount point
    if [ -d "$MOUNT_POINT" ]; then
        rmdir "$MOUNT_POINT" 2>/dev/null || true
    fi

    # Remove temporary directory
    if [ -d "$TEMP_DIR" ]; then
        rm -rf "$TEMP_DIR"
    fi

    # Remove test files from S3
    echo "Removing test files from S3..."
    for i in $(seq 1 $NUM_TEST_FILES); do
        aws s3 rm "s3://${TEST_BUCKET}/${TEST_PREFIX}/testfile${i}.dat" \
            --region "$TEST_REGION" 2>/dev/null || true
    done

    echo -e "${GREEN}Cleanup complete${NC}"
}

# Set up cleanup trap
trap cleanup EXIT INT TERM

# Print section header
print_section() {
    echo ""
    echo "=========================================="
    echo "$1"
    echo "=========================================="
}

# Print test result
print_result() {
    if [ "$1" -eq 0 ]; then
        echo -e "${GREEN}✓ PASS${NC}: $2"
    else
        echo -e "${RED}✗ FAIL${NC}: $2"
        exit 1
    fi
}

# Check prerequisites
print_section "Checking Prerequisites"

# Check for AWS CLI
if ! command -v aws &> /dev/null; then
    echo -e "${RED}Error: AWS CLI not found${NC}"
    exit 1
fi
echo -e "${GREEN}✓${NC} AWS CLI found"

# Check AWS credentials
if ! aws sts get-caller-identity --region "$TEST_REGION" &> /dev/null; then
    echo -e "${RED}Error: AWS credentials not configured${NC}"
    exit 1
fi
echo -e "${GREEN}✓${NC} AWS credentials configured"

# Check for Valkyrie-FS binary
if [ ! -f "$VALKYRIE_BIN" ]; then
    echo -e "${RED}Error: Valkyrie-FS binary not found at $VALKYRIE_BIN${NC}"
    echo "Please build the project first: cmake -B build && cmake --build build"
    exit 1
fi
echo -e "${GREEN}✓${NC} Valkyrie-FS binary found"

# Check if bucket exists
if ! aws s3 ls "s3://${TEST_BUCKET}" --region "$TEST_REGION" &> /dev/null; then
    echo -e "${YELLOW}Warning: Bucket $TEST_BUCKET not found, attempting to create...${NC}"
    if aws s3 mb "s3://${TEST_BUCKET}" --region "$TEST_REGION"; then
        echo -e "${GREEN}✓${NC} Bucket created"
    else
        echo -e "${RED}Error: Failed to create bucket${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}✓${NC} S3 bucket accessible"
fi

# Get CPU count (macOS compatible)
CPU_COUNT=$(sysctl -n hw.ncpu)
echo -e "${GREEN}✓${NC} System has $CPU_COUNT CPUs"

# Create temporary directory
print_section "Setting Up Test Environment"
mkdir -p "$TEMP_DIR"
echo "Temporary directory: $TEMP_DIR"

# Create mount point
mkdir -p "$MOUNT_POINT"
echo "Mount point: $MOUNT_POINT"

# Generate test files and upload to S3
print_section "Generating and Uploading Test Files"

declare -a TEST_FILES
declare -a TEST_MD5S

for i in $(seq 0 $((NUM_TEST_FILES-1))); do
    idx=$((i + 1))  # For filename numbering
    filename="testfile${idx}.dat"
    filepath="$TEMP_DIR/$filename"
    s3path="s3://${TEST_BUCKET}/${TEST_PREFIX}/$filename"

    echo "Generating $filename (10MB)..."
    dd if=/dev/urandom of="$filepath" bs=1m count=10 2>/dev/null

    echo "Calculating MD5..."
    if command -v md5sum &> /dev/null; then
        md5=$(md5sum "$filepath" | awk '{print $1}')
    else
        # macOS uses 'md5' instead of 'md5sum'
        md5=$(md5 -q "$filepath")
    fi

    echo "Uploading to S3: $s3path"
    aws s3 cp "$filepath" "$s3path" --region "$TEST_REGION"

    TEST_FILES[$i]="$filename"
    TEST_MD5S[$i]="$md5"
    echo -e "${GREEN}✓${NC} $filename uploaded (MD5: $md5)"
done

# Start Valkyrie-FS
print_section "Starting Valkyrie-FS"

echo "Starting Valkyrie-FS with:"
echo "  Bucket: $TEST_BUCKET"
echo "  Region: $TEST_REGION"
echo "  Mount: $MOUNT_POINT"
echo "  Prefix: $TEST_PREFIX"

# Start in background and capture PID
sudo "$VALKYRIE_BIN" \
    --bucket "$TEST_BUCKET" \
    --region "$TEST_REGION" \
    --mount "$MOUNT_POINT" \
    --s3-prefix "$TEST_PREFIX" \
    --cache-size 512M \
    --workers 4 \
    > "$TEMP_DIR/valkyrie.log" 2>&1 &

VALKYRIE_PID=$!
echo "Valkyrie-FS started (PID: $VALKYRIE_PID)"

# Wait for mount to be ready
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

# Test 1: List files in mount
print_section "Test 1: List Files"

echo "Listing files in $MOUNT_POINT..."
files_found=$(ls -la "$MOUNT_POINT" | grep -c "testfile" || true)

if [ "$files_found" -eq "$NUM_TEST_FILES" ]; then
    print_result 0 "Found all $NUM_TEST_FILES test files"
else
    print_result 1 "Expected $NUM_TEST_FILES files, found $files_found"
fi

ls -lh "$MOUNT_POINT"

# Test 2: Read first file and verify MD5
print_section "Test 2: MD5 Verification"

testfile="${TEST_FILES[0]}"
expected_md5="${TEST_MD5S[0]}"

echo "Reading $testfile from mounted filesystem..."
mounted_file="$MOUNT_POINT/$testfile"

if [ ! -f "$mounted_file" ]; then
    print_result 1 "File $testfile not found in mount"
fi

echo "Calculating MD5 of mounted file..."
if command -v md5sum &> /dev/null; then
    actual_md5=$(md5sum "$mounted_file" | awk '{print $1}')
else
    # macOS uses 'md5' instead of 'md5sum'
    actual_md5=$(md5 -q "$mounted_file")
fi

echo "Expected MD5: $expected_md5"
echo "Actual MD5:   $actual_md5"

if [ "$expected_md5" = "$actual_md5" ]; then
    print_result 0 "MD5 checksum matches"
else
    print_result 1 "MD5 checksum mismatch"
fi

# Test 3: Sequential reads to test prefetching
print_section "Test 3: Sequential Read Performance"

echo "Reading all files sequentially to test prefetching..."
start_time=$(date +%s)

for i in $(seq 0 $((NUM_TEST_FILES-1))); do
    testfile="${TEST_FILES[$i]}"
    expected_md5="${TEST_MD5S[$i]}"
    mounted_file="$MOUNT_POINT/$testfile"

    echo -n "Reading $testfile... "

    if command -v md5sum &> /dev/null; then
        actual_md5=$(md5sum "$mounted_file" | awk '{print $1}')
    else
        actual_md5=$(md5 -q "$mounted_file")
    fi

    if [ "$expected_md5" = "$actual_md5" ]; then
        echo -e "${GREEN}✓${NC}"
    else
        echo -e "${RED}✗${NC}"
        print_result 1 "MD5 mismatch for $testfile"
    fi
done

end_time=$(date +%s)
duration=$((end_time - start_time))

echo "Sequential read of ${NUM_TEST_FILES} files (30MB total) completed in ${duration}s"
print_result 0 "Sequential reads completed successfully"

# Final report
print_section "Test Summary"

echo -e "${GREEN}All tests passed!${NC}"
echo ""
echo "Statistics:"
echo "  Files tested: $NUM_TEST_FILES"
echo "  Total data: $((NUM_TEST_FILES * TEST_FILE_SIZE / 1048576))MB"
echo "  Read duration: ${duration}s"
echo "  Throughput: $((NUM_TEST_FILES * TEST_FILE_SIZE / duration / 1048576))MB/s (approximate)"
echo ""
echo "Valkyrie-FS log available at: $TEMP_DIR/valkyrie.log"

exit 0

#!/bin/bash
# Performance Benchmarking Script for Valkyrie-FS
# Measures sequential read throughput, cache hit rates, and time to first byte

set -e  # Exit on error
set -u  # Exit on undefined variable

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Benchmark configuration
TEST_BUCKET="${TEST_BUCKET:-valkyrie-test-bucket}"
TEST_REGION="${TEST_REGION:-us-east-1}"
MOUNT_POINT="${MOUNT_POINT:-/tmp/valkyrie-bench}"
TEST_PREFIX="benchmark-$(date +%s)"
VALKYRIE_BIN="./build/bin/valkyrie"
TEMP_DIR="/tmp/valkyrie-bench-$$"
VALKYRIE_PID=""

# Performance test configuration
TEST_FILE_SIZE_MB="${TEST_FILE_SIZE_MB:-10}"  # Size of each test file in MB
NUM_TEST_FILES="${NUM_TEST_FILES:-5}"         # Number of test files
CACHE_SIZE="${CACHE_SIZE:-512M}"              # Cache size for Valkyrie-FS
NUM_WORKERS="${NUM_WORKERS:-4}"               # Number of worker threads

# Results storage
declare -a S3_DIRECT_TIMES
declare -a COLD_READ_TIMES
declare -a WARM_READ_TIMES
declare -a COLD_TTFB_TIMES
declare -a WARM_TTFB_TIMES

# Cleanup function
cleanup() {
    echo ""
    echo -e "${YELLOW}Cleaning up...${NC}"

    # Unmount filesystem
    if mount | grep -q "$MOUNT_POINT"; then
        echo "Unmounting $MOUNT_POINT..."
        if [[ "$OSTYPE" == "darwin"* ]]; then
            umount "$MOUNT_POINT" 2>/dev/null || sudo umount "$MOUNT_POINT" 2>/dev/null || true
        else
            fusermount -u "$MOUNT_POINT" 2>/dev/null || sudo umount "$MOUNT_POINT" 2>/dev/null || true
        fi
        sleep 1
    fi

    # Kill Valkyrie-FS process
    if [ -n "$VALKYRIE_PID" ] && kill -0 "$VALKYRIE_PID" 2>/dev/null; then
        echo "Stopping Valkyrie-FS (PID: $VALKYRIE_PID)..."
        sudo kill "$VALKYRIE_PID" 2>/dev/null || kill "$VALKYRIE_PID" 2>/dev/null || true
        sleep 1
        sudo kill -9 "$VALKYRIE_PID" 2>/dev/null || kill -9 "$VALKYRIE_PID" 2>/dev/null || true
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
    if [ "${CLEANUP_S3:-yes}" = "yes" ]; then
        echo "Removing test files from S3..."
        for i in $(seq 1 $NUM_TEST_FILES); do
            aws s3 rm "s3://${TEST_BUCKET}/${TEST_PREFIX}/testfile${i}.dat" \
                --region "$TEST_REGION" 2>/dev/null || true
        done
    else
        echo "Keeping test files in S3 (s3://${TEST_BUCKET}/${TEST_PREFIX}/)"
    fi

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

# Print banner
print_banner() {
    echo ""
    echo "╔════════════════════════════════════════╗"
    echo "║   Valkyrie-FS Performance Benchmark   ║"
    echo "╚════════════════════════════════════════╝"
    echo ""
}

# Calculate statistics
calculate_stats() {
    local -n arr=$1
    local sum=0
    local count=${#arr[@]}

    if [ "$count" -eq 0 ]; then
        echo "0"
        return
    fi

    for val in "${arr[@]}"; do
        sum=$(echo "$sum + $val" | bc -l)
    done

    echo "scale=3; $sum / $count" | bc -l
}

# Format time in milliseconds
format_ms() {
    local time_ms=$1
    if [ $(echo "$time_ms < 1000" | bc -l) -eq 1 ]; then
        printf "%.0fms" "$time_ms"
    else
        printf "%.2fs" "$(echo "scale=2; $time_ms / 1000" | bc -l)"
    fi
}

# Calculate throughput in MB/s
calculate_throughput() {
    local size_mb=$1
    local time_sec=$2
    if [ $(echo "$time_sec < 0.001" | bc -l) -eq 1 ]; then
        echo "N/A"
    else
        echo "scale=2; $size_mb / $time_sec" | bc -l
    fi
}

print_banner

# Check prerequisites
print_section "Checking Prerequisites"

# Check for required tools
if ! command -v bc &> /dev/null; then
    echo -e "${RED}Error: 'bc' calculator not found${NC}"
    echo "Please install bc: brew install bc (macOS) or apt-get install bc (Linux)"
    exit 1
fi
echo -e "${GREEN}✓${NC} bc calculator found"

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

# Display configuration
print_section "Benchmark Configuration"
echo "Test Parameters:"
echo "  Bucket:           $TEST_BUCKET"
echo "  Region:           $TEST_REGION"
echo "  File Size:        ${TEST_FILE_SIZE_MB}MB"
echo "  Number of Files:  $NUM_TEST_FILES"
echo "  Total Dataset:    $((TEST_FILE_SIZE_MB * NUM_TEST_FILES))MB"
echo "  Cache Size:       $CACHE_SIZE"
echo "  Worker Threads:   $NUM_WORKERS"
echo ""

# Create temporary directory
mkdir -p "$TEMP_DIR"
echo "Temporary directory: $TEMP_DIR"

# Create mount point
mkdir -p "$MOUNT_POINT"
echo "Mount point: $MOUNT_POINT"

# Generate test files and upload to S3
print_section "Preparing Test Dataset"

declare -a TEST_FILES
declare -a TEST_MD5S

for i in $(seq 0 $((NUM_TEST_FILES-1))); do
    idx=$((i + 1))
    filename="testfile${idx}.dat"
    filepath="$TEMP_DIR/$filename"
    s3path="s3://${TEST_BUCKET}/${TEST_PREFIX}/$filename"

    echo -n "Generating $filename (${TEST_FILE_SIZE_MB}MB)... "
    dd if=/dev/urandom of="$filepath" bs=1m count="$TEST_FILE_SIZE_MB" 2>/dev/null
    echo "done"

    echo -n "Calculating MD5... "
    if command -v md5sum &> /dev/null; then
        md5=$(md5sum "$filepath" | awk '{print $1}')
    else
        # macOS uses 'md5' instead of 'md5sum'
        md5=$(md5 -q "$filepath")
    fi
    echo "$md5"

    echo -n "Uploading to S3... "
    aws s3 cp "$filepath" "$s3path" --region "$TEST_REGION" --quiet
    echo -e "${GREEN}✓${NC}"

    TEST_FILES[$i]="$filename"
    TEST_MD5S[$i]="$md5"
done

echo ""
echo -e "${GREEN}✓${NC} Dataset prepared: $((NUM_TEST_FILES * TEST_FILE_SIZE_MB))MB uploaded to S3"

# Benchmark 0: Direct S3 Download (Baseline)
print_section "Benchmark 0: Direct S3 Download (Baseline)"
echo "Testing direct S3 downloads without Valkyrie-FS..."
echo "This provides baseline performance for comparison."
echo ""

for i in $(seq 0 $((NUM_TEST_FILES-1))); do
    idx=$((i + 1))
    filename="${TEST_FILES[$i]}"
    s3path="s3://${TEST_BUCKET}/${TEST_PREFIX}/$filename"
    download_path="$TEMP_DIR/s3_direct_$filename"

    echo -n "[$idx/$NUM_TEST_FILES] Downloading $filename directly from S3... "

    # Time the download
    read_start=$(date +%s%3N)
    aws s3 cp "$s3path" "$download_path" --region "$TEST_REGION" --quiet
    read_end=$(date +%s%3N)
    read_ms=$((read_end - read_start))
    S3_DIRECT_TIMES[$i]=$read_ms

    throughput=$(calculate_throughput "$TEST_FILE_SIZE_MB" "$(echo "scale=3; $read_ms / 1000" | bc -l)")

    echo -e "${GREEN}✓${NC} Time: $(format_ms $read_ms), Throughput: ${throughput}MB/s"

    # Clean up downloaded file to save disk space
    rm -f "$download_path"
done

# Calculate S3 direct download statistics
avg_s3_time=$(calculate_stats S3_DIRECT_TIMES)
avg_s3_throughput=$(calculate_throughput "$TEST_FILE_SIZE_MB" "$(echo "scale=3; $avg_s3_time / 1000" | bc -l)")

echo ""
echo "S3 Direct Download Results:"
echo "  Avg Download Time:  $(format_ms $avg_s3_time)"
echo "  Avg Throughput:     ${avg_s3_throughput}MB/s"
echo ""
echo "This baseline will be compared against Valkyrie-FS performance."

# Start Valkyrie-FS
print_section "Starting Valkyrie-FS"

echo "Mounting filesystem with:"
echo "  Bucket: $TEST_BUCKET"
echo "  Region: $TEST_REGION"
echo "  Mount: $MOUNT_POINT"
echo "  Prefix: $TEST_PREFIX"
echo "  Cache: $CACHE_SIZE"
echo "  Workers: $NUM_WORKERS"
echo ""

# Start in background and capture PID
sudo "$VALKYRIE_BIN" \
    --bucket "$TEST_BUCKET" \
    --region "$TEST_REGION" \
    --mount "$MOUNT_POINT" \
    --s3-prefix "$TEST_PREFIX" \
    --cache-size "$CACHE_SIZE" \
    --workers "$NUM_WORKERS" \
    > "$TEMP_DIR/valkyrie.log" 2>&1 &

VALKYRIE_PID=$!
echo "Valkyrie-FS started (PID: $VALKYRIE_PID)"

# Wait for mount to be ready
echo -n "Waiting for filesystem to mount"
for i in {1..30}; do
    if mount | grep -q "$MOUNT_POINT"; then
        echo ""
        echo -e "${GREEN}✓${NC} Filesystem mounted"
        break
    fi
    if ! kill -0 "$VALKYRIE_PID" 2>/dev/null; then
        echo ""
        echo -e "${RED}Error: Valkyrie-FS process died${NC}"
        cat "$TEMP_DIR/valkyrie.log"
        exit 1
    fi
    echo -n "."
    sleep 1
done

if ! mount | grep -q "$MOUNT_POINT"; then
    echo ""
    echo -e "${RED}Error: Filesystem failed to mount after 30 seconds${NC}"
    cat "$TEMP_DIR/valkyrie.log"
    exit 1
fi

# Give it a moment to stabilize
sleep 2

# Benchmark 1: Cold Cache - Time to First Byte and Full Read
print_section "Benchmark 1: Cold Cache Performance"
echo "Testing cold cache reads (first access from S3)..."
echo ""

for i in $(seq 0 $((NUM_TEST_FILES-1))); do
    testfile="${TEST_FILES[$i]}"
    mounted_file="$MOUNT_POINT/$testfile"

    echo -n "[$((i+1))/$NUM_TEST_FILES] Reading $testfile... "

    # Time to first byte (read first 1KB)
    ttfb_start=$(date +%s%3N)
    dd if="$mounted_file" of=/dev/null bs=1024 count=1 2>/dev/null
    ttfb_end=$(date +%s%3N)
    ttfb_ms=$((ttfb_end - ttfb_start))
    COLD_TTFB_TIMES[$i]=$ttfb_ms

    # Full file read
    read_start=$(date +%s%3N)
    dd if="$mounted_file" of=/dev/null bs=1m 2>/dev/null
    read_end=$(date +%s%3N)
    read_ms=$((read_end - read_start))
    COLD_READ_TIMES[$i]=$read_ms

    throughput=$(calculate_throughput "$TEST_FILE_SIZE_MB" "$(echo "scale=3; $read_ms / 1000" | bc -l)")

    echo -e "${GREEN}✓${NC} TTFB: $(format_ms $ttfb_ms), Total: $(format_ms $read_ms), Throughput: ${throughput}MB/s"
done

# Calculate cold cache statistics
avg_cold_ttfb=$(calculate_stats COLD_TTFB_TIMES)
avg_cold_read=$(calculate_stats COLD_READ_TIMES)
avg_cold_throughput=$(calculate_throughput "$TEST_FILE_SIZE_MB" "$(echo "scale=3; $avg_cold_read / 1000" | bc -l)")

echo ""
echo "Cold Cache Results:"
echo "  Avg Time to First Byte: $(format_ms $avg_cold_ttfb)"
echo "  Avg Full Read Time:     $(format_ms $avg_cold_read)"
echo "  Avg Throughput:         ${avg_cold_throughput}MB/s"

# Benchmark 2: Warm Cache - Repeat Reads
print_section "Benchmark 2: Warm Cache Performance"
echo "Testing warm cache reads (cached data)..."
echo ""

for i in $(seq 0 $((NUM_TEST_FILES-1))); do
    testfile="${TEST_FILES[$i]}"
    mounted_file="$MOUNT_POINT/$testfile"

    echo -n "[$((i+1))/$NUM_TEST_FILES] Reading $testfile... "

    # Time to first byte (read first 1KB)
    ttfb_start=$(date +%s%3N)
    dd if="$mounted_file" of=/dev/null bs=1024 count=1 2>/dev/null
    ttfb_end=$(date +%s%3N)
    ttfb_ms=$((ttfb_end - ttfb_start))
    WARM_TTFB_TIMES[$i]=$ttfb_ms

    # Full file read
    read_start=$(date +%s%3N)
    dd if="$mounted_file" of=/dev/null bs=1m 2>/dev/null
    read_end=$(date +%s%3N)
    read_ms=$((read_end - read_start))
    WARM_READ_TIMES[$i]=$read_ms

    throughput=$(calculate_throughput "$TEST_FILE_SIZE_MB" "$(echo "scale=3; $read_ms / 1000" | bc -l)")

    echo -e "${GREEN}✓${NC} TTFB: $(format_ms $ttfb_ms), Total: $(format_ms $read_ms), Throughput: ${throughput}MB/s"
done

# Calculate warm cache statistics
avg_warm_ttfb=$(calculate_stats WARM_TTFB_TIMES)
avg_warm_read=$(calculate_stats WARM_READ_TIMES)
avg_warm_throughput=$(calculate_throughput "$TEST_FILE_SIZE_MB" "$(echo "scale=3; $avg_warm_read / 1000" | bc -l)")

echo ""
echo "Warm Cache Results:"
echo "  Avg Time to First Byte: $(format_ms $avg_warm_ttfb)"
echo "  Avg Full Read Time:     $(format_ms $avg_warm_read)"
echo "  Avg Throughput:         ${avg_warm_throughput}MB/s"

# Calculate cache improvement
ttfb_improvement=$(echo "scale=1; (($avg_cold_ttfb - $avg_warm_ttfb) / $avg_cold_ttfb) * 100" | bc -l)
read_improvement=$(echo "scale=1; (($avg_cold_read - $avg_warm_read) / $avg_cold_read) * 100" | bc -l)

echo ""
echo "Cache Effectiveness:"
echo "  TTFB Improvement:       ${ttfb_improvement}%"
echo "  Read Speed Improvement: ${read_improvement}%"

# Benchmark 3: Data Integrity Verification
print_section "Benchmark 3: Data Integrity Verification"
echo "Verifying MD5 checksums to ensure data integrity..."
echo ""

seq_start=$(date +%s%3N)

for i in $(seq 0 $((NUM_TEST_FILES-1))); do
    testfile="${TEST_FILES[$i]}"
    expected_md5="${TEST_MD5S[$i]}"
    mounted_file="$MOUNT_POINT/$testfile"

    echo -n "[$((i+1))/$NUM_TEST_FILES] Verifying $testfile... "

    if command -v md5sum &> /dev/null; then
        actual_md5=$(md5sum "$mounted_file" | awk '{print $1}')
    else
        actual_md5=$(md5 -q "$mounted_file")
    fi

    if [ "$expected_md5" = "$actual_md5" ]; then
        echo -e "${GREEN}✓${NC} MD5 verified"
    else
        echo -e "${RED}✗${NC} MD5 mismatch!"
        echo "  Expected: $expected_md5"
        echo "  Got:      $actual_md5"
        exit 1
    fi
done

seq_end=$(date +%s%3N)
seq_duration_ms=$((seq_end - seq_start))
seq_duration_s=$(echo "scale=3; $seq_duration_ms / 1000" | bc -l)

total_mb=$((TEST_FILE_SIZE_MB * NUM_TEST_FILES))
seq_throughput=$(calculate_throughput "$total_mb" "$seq_duration_s")

echo ""
echo "Data Integrity Results:"
echo "  Files Verified:    ${NUM_TEST_FILES}"
echo "  Total Data:        ${total_mb}MB"
echo "  Verification Time: $(format_ms $seq_duration_ms)"
echo "  Throughput:        ${seq_throughput}MB/s"
echo -e "  Status:            ${GREEN}All checksums valid${NC}"

# Final Performance Report
print_section "Performance Summary"

echo ""
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                    BENCHMARK RESULTS                           ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""
echo "Configuration:"
echo "  Dataset:           ${NUM_TEST_FILES} files × ${TEST_FILE_SIZE_MB}MB = ${total_mb}MB"
echo "  Cache Size:        $CACHE_SIZE"
echo "  Workers:           $NUM_WORKERS"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "S3 Direct Download (Baseline):"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
printf "  Avg Download Time:   %20s\n" "$(format_ms $avg_s3_time)"
printf "  Avg Throughput:      %20s\n" "${avg_s3_throughput}MB/s"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Cold Cache (First Access via Valkyrie-FS):"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
printf "  Time to First Byte:  %20s\n" "$(format_ms $avg_cold_ttfb)"
printf "  Avg Read Time:       %20s\n" "$(format_ms $avg_cold_read)"
printf "  Avg Throughput:      %20s\n" "${avg_cold_throughput}MB/s"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Warm Cache (Cached Reads):"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
printf "  Time to First Byte:  %20s\n" "$(format_ms $avg_warm_ttfb)"
printf "  Avg Read Time:       %20s\n" "$(format_ms $avg_warm_read)"
printf "  Avg Throughput:      %20s\n" "${avg_warm_throughput}MB/s"
printf "  Speedup vs Cold:     %20s\n" "${read_improvement}%"

# Calculate and display speedup vs S3 baseline
speedup_vs_s3_cold=$(echo "scale=2; $avg_s3_time / $avg_cold_read" | bc -l)
speedup_vs_s3_warm=$(echo "scale=2; $avg_s3_time / $avg_warm_read" | bc -l)
printf "  Speedup vs S3:       %20s\n" "${speedup_vs_s3_warm}x"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Data Integrity Verification:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
printf "  Files Verified:      %20s\n" "${NUM_TEST_FILES}"
printf "  Total Time:          %20s\n" "$(format_ms $seq_duration_ms)"
printf "  Throughput:          %20s\n" "${seq_throughput}MB/s"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo -e "${GREEN}✓ Benchmark completed successfully${NC}"
echo ""
echo "Log file: $TEMP_DIR/valkyrie.log"
echo "S3 Prefix: s3://${TEST_BUCKET}/${TEST_PREFIX}/"
echo ""

exit 0

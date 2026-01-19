#!/bin/bash
# Integration test for S3 downloads
# Requires: AWS credentials configured, test bucket with sample files

set -e

TEST_BUCKET="${TEST_BUCKET:-valkyrie-test-bucket}"
TEST_REGION="${TEST_REGION:-us-west-2}"

echo "=== Valkyrie S3 Integration Test ==="
echo "Bucket: $TEST_BUCKET"
echo "Region: $TEST_REGION"
echo

# Check AWS credentials
if ! aws sts get-caller-identity &> /dev/null; then
    echo "ERROR: AWS credentials not configured"
    echo "Run: aws configure"
    exit 1
fi

# Check if bucket exists
if ! aws s3 ls "s3://$TEST_BUCKET" &> /dev/null; then
    echo "ERROR: Bucket $TEST_BUCKET does not exist or is not accessible"
    exit 1
fi

# Create test files in S3
echo "Creating test files in S3..."
dd if=/dev/urandom of=/tmp/test_shard_001.bin bs=1M count=10 2>/dev/null
dd if=/dev/urandom of=/tmp/test_shard_002.bin bs=1M count=10 2>/dev/null
dd if=/dev/urandom of=/tmp/test_shard_003.bin bs=1M count=10 2>/dev/null

aws s3 cp /tmp/test_shard_001.bin "s3://$TEST_BUCKET/test/shard_001.bin"
aws s3 cp /tmp/test_shard_002.bin "s3://$TEST_BUCKET/test/shard_002.bin"
aws s3 cp /tmp/test_shard_003.bin "s3://$TEST_BUCKET/test/shard_003.bin"

echo "Test files created successfully"
echo
echo "To test Valkyrie manually:"
echo "  sudo ./build/bin/valkyrie \\"
echo "    --mount /tmp/valkyrie_test \\"
echo "    --bucket $TEST_BUCKET \\"
echo "    --s3-prefix test \\"
echo "    --region $TEST_REGION \\"
echo "    --cache-size 1G \\"
echo "    --workers 4"
echo
echo "Then in another terminal:"
echo "  ls /tmp/valkyrie_test/"
echo "  md5sum /tmp/valkyrie_test/shard_001.bin"
echo "  md5sum /tmp/test_shard_001.bin"

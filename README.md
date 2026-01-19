# Valkyrie-FS

High-performance S3 prefetching FUSE filesystem for ML/AI training workloads.

## Problem

AI training workloads read thousands of sequential shards from S3. Without prefetching, GPUs sit idle waiting for I/O, achieving <50% utilization.

## Solution

Valkyrie-FS intelligently prefetches upcoming shards while the GPU processes current data, achieving 95%+ GPU utilization.

## Features

- **Chunk-based caching**: 4MB chunks for instant response on large files
- **Two-tier cache**: Hot (LRU) + Prefetch (FIFO) zones prevent cache pollution
- **Intelligent prediction**: Sequential pattern detection + manifest support
- **Production-grade**: Prometheus metrics, structured logging, trace files

## Build

### Dependencies

```bash
sudo apt install libfuse3-dev cmake g++ libssl-dev libcurl4-openssl-dev
```

### Install AWS SDK for C++

Ubuntu/Debian:
```bash
sudo apt install libaws-cpp-sdk-s3-dev
```

macOS:
```bash
brew install aws-sdk-cpp
```

From source (if package not available):
```bash
git clone --depth 1 --branch 1.11.200 https://github.com/aws/aws-sdk-cpp
cd aws-sdk-cpp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_ONLY="s3" -DENABLE_TESTING=OFF
make -j$(nproc)
sudo make install
```

### Compile

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Run

```bash
sudo ./bin/valkyrie \
  --mount /mnt/valkyrie \
  --bucket my-training-data \
  --cache-size 16G \
  --workers 8
```

## Testing

### Unit Tests

```bash
cd build
make test_types && ./bin/test_types
make test_queue && ./bin/test_queue
make test_cache_manager && ./bin/test_cache_manager
make test_s3_mock && ./bin/test_s3_mock
```

### S3 Integration Test

Requires AWS credentials and a test bucket:

```bash
export TEST_BUCKET=your-test-bucket
export TEST_REGION=us-west-2
./scripts/test_s3_integration.sh
```

## Architecture

See `docs/plans/2026-01-18-valkyrie-fs-design.md`

## Status

Phase 1: Build system ✅
Phase 2: Core data structures ✅
Phase 3: S3 Worker Pool (in progress)

## License

MIT (placeholder)

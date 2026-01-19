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

AWS SDK for C++ (will be added in Phase 3)

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

## Architecture

See `docs/plans/2026-01-18-valkyrie-fs-design.md`

## Status

Phase 1: Build system ✅
Phase 2: Core data structures (in progress)

## License

MIT (placeholder)

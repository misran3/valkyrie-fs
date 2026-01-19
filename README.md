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

Ubuntu/Debian:
```bash
sudo apt install libfuse3-dev cmake g++ libssl-dev libcurl4-openssl-dev
```

macOS:
```bash
brew install macfuse cmake
```

Note: macFUSE may require system reboot after installation.

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
sudo ./build/bin/valkyrie \
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

## Usage

### Mounting

Mount an S3 bucket as a local filesystem:

```bash
sudo ./build/bin/valkyrie \
  --mount /mnt/valkyrie \
  --bucket my-training-data \
  --cache-size 16G \
  --workers 8 \
  --lookahead 32 \
  --region us-west-2
```

The mount point must exist and be empty:
```bash
sudo mkdir -p /mnt/valkyrie
```

### Accessing Files

Once mounted, access files like any local directory:

```bash
# List files
ls -lh /mnt/valkyrie/shards/

# Read a file
cat /mnt/valkyrie/data/shard_0001.tar

# Stream to training script
python train.py --data-path /mnt/valkyrie/shards/
```

Valkyrie-FS detects sequential access patterns and prefetches upcoming files automatically.

### Using a Manifest

For best performance, provide a manifest file listing files in training order:

```bash
# Create manifest with training file order
cat > /tmp/training_manifest.txt <<EOF
shards/shard_0001.tar
shards/shard_0002.tar
shards/shard_0003.tar
EOF

# Mount with manifest
sudo ./build/bin/valkyrie \
  --mount /mnt/valkyrie \
  --bucket my-training-data \
  --manifest /tmp/training_manifest.txt \
  --cache-size 16G \
  --workers 8
```

With a manifest, prefetching starts immediately without waiting for pattern detection.

### Unmounting

Unmount when finished:

```bash
sudo umount /mnt/valkyrie
```

On Linux, you can also use:
```bash
fusermount -u /mnt/valkyrie
```

## Performance Tips

### Cache Size

Set cache size based on your shard size and prefetch needs:

```bash
# Small shards (< 100MB): 4-8GB cache
--cache-size 4G

# Medium shards (100-500MB): 8-16GB cache
--cache-size 16G

# Large shards (> 500MB): 32GB+ cache
--cache-size 32G
```

Formula: `cache_size = shard_size * (lookahead + 2)`

The "+2" accounts for the current file being read plus one buffer.

### Worker Threads

Configure workers based on your CPU and network:

```bash
# Network-bound (1 Gbps): 4-8 workers
--workers 4

# Balanced (10 Gbps): 8-16 workers
--workers 8

# CPU-bound or very fast network: 16-32 workers
--workers 16
```

More workers help when S3 latency is high or you need aggressive prefetching.

### Lookahead Distance

Set lookahead based on your read speed vs network speed:

```bash
# Fast local NVMe, slow network: prefetch more
--lookahead 64

# Balanced: default works well
--lookahead 32

# Very fast network, slower processing: prefetch less
--lookahead 16
```

Monitor cache hit rate in metrics. Increase lookahead if you see cache misses.

### Manifest Files

Always use a manifest for training workloads:

1. **Eliminates cold start**: Prefetching starts immediately
2. **Perfect prediction**: No pattern detection needed
3. **Optimal scheduling**: Workers load files in exact order

Generate manifest from your dataloader:

```python
# PyTorch example
with open('manifest.txt', 'w') as f:
    for shard_path in dataset.get_shard_paths():
        f.write(f"{shard_path}\n")
```

## Troubleshooting

### Mount Failures

**Error: "cannot mount: /mnt/valkyrie not found"**
- Create the mount point: `sudo mkdir -p /mnt/valkyrie`

**Error: "Transport endpoint is not connected"**
- Previous mount still active. Unmount first: `sudo umount /mnt/valkyrie`
- On macOS, you may need to force unmount: `sudo umount -f /mnt/valkyrie`

**Error: "Permission denied"**
- FUSE filesystem requires root access. Use `sudo`
- On macOS, check that macFUSE kernel extension is loaded: `kextstat | grep fuse`

**Error: "AWS credentials not found"**
- Set AWS credentials via environment variables:
  ```bash
  export AWS_ACCESS_KEY_ID=your_key
  export AWS_SECRET_ACCESS_KEY=your_secret
  ```
- Or configure via `aws configure`

### Slow Reads

**First read is slow, then fast**
- Normal behavior. First read triggers prefetch, subsequent reads hit cache
- Use `--manifest` to start prefetching before first read

**All reads are slow**
- Check cache size: `--cache-size` may be too small for your shards
- Check workers: Increase `--workers` if CPU allows
- Check S3 region: Use `--region` closest to your location
- Verify sequential access: Random access defeats prefetching

**Cache thrashing**
- Reduce `--lookahead` if working set exceeds cache size
- Increase `--cache-size` if possible

### High Cache Miss Rate

Check Prometheus metrics at `http://localhost:9090/metrics`:

```bash
curl http://localhost:9090/metrics | grep cache_hit_rate
```

**Cache hit rate < 80%**
- Increase `--cache-size`
- Increase `--lookahead` to prefetch earlier
- Verify access is sequential (not random)

**Prefetch queue empty**
- Pattern not detected yet (first few files)
- Use `--manifest` for immediate prefetching
- Check logs for pattern detection messages

**High memory usage**
- Reduce `--cache-size`
- Reduce `--lookahead`
- Check for memory leaks in logs

### Debugging

Run with basic mount (diagnostic info printed to stdout/stderr):

```bash
sudo ./build/bin/valkyrie --mount /mnt/valkyrie --bucket my-data --region us-west-2
```

Check trace files for detailed operation logs:
```bash
ls -lh /tmp/valkyrie_trace_*.json
```

View Prometheus metrics:
```bash
curl http://localhost:9090/metrics
```

## Status

Phase 1: Build system ✅
Phase 2: Core data structures ✅
Phase 3: S3 Worker Pool ✅
Phase 4: Prefetch Engine ✅
Phase 5: FUSE Filesystem ✅
Phase 6: Metrics & Observability ✅
Phase 7: Command-line Interface ✅
Phase 8: Integration & Testing ✅

## License

MIT (placeholder)

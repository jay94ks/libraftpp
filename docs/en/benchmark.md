# Native Benchmark

[한국어](../ko/benchmark.md) · [Node API](node.md) · [libuv I/O](uv_io.md) · [Owning Diagnostics](errors.md)

`raftpp-benchmark submit` measures sequential submissions through `node`, `uv_io`, and the native writer. The configuration has one voter and a no-op FSM; commands commit through the node's own durable I/O. The process also initializes a real TCP transport using the configured address. Temporary state is isolated beneath the selected directory and removed after actual shutdown.

## Build and Invocation

```sh
cmake -S . -B build -DRAFTPP_WITH_UV=ON -DRAFTPP_BUILD_BENCHMARKS=ON
cmake --build build --target raftpp_benchmark --parallel
./build/tools/benchmark/raftpp-benchmark submit --dir /tmp --buf 2048 --size 8M
```

`RAFTPP_BUILD_BENCHMARKS` defaults to OFF and requires UV on Linux. Fixture, LZ4, examples, and tests are independent options. The tool links the C++ library. Use `raftpp-benchmark --help` or `raftpp-benchmark submit --help` for usage.

| Option | Contract |
| --- | --- |
| `--dir DIR`, `-d DIR` | Existing directory for a private `bench-XXXXXX` workspace. Default `.`. Unrelated files are retained. |
| `--buf BYTES`, `-b BYTES` | Encoded entry budget, 48..1048576 bytes, divisible by eight. Default 2048. |
| `--size BYTES`, `-s BYTES` | Positive encoded total, at most UINT32_MAX, divisible by 4096 and by the entry budget. Default 8 MiB. |
| `--address HOST:PORT` | Single-voter TCP bind/advertised address. Default `127.0.0.1:0` uses an ephemeral local port. |
| `--timeout MS` | Positive measurement deadline after startup, including prepare-pool waiting. Default 30000 ms. |
| `--help`, `-?` | Show command usage. |

Sizes accept unsigned decimal bytes and uppercase `K`, `M`, or `G` suffixes representing powers of 1024. Long options accept `--name=value`; short value options also accept attached values such as `-b4096`. A duration is a plain positive decimal integer in milliseconds. The operation count is exactly `size / buf`, including one-operation runs.

## Submit Measurement Boundaries

Initialization, bootstrap, directory preparation, and shutdown occur outside submission latency samples. Each sample starts before command-buffer/callback construction and ends when that command's apply completion is delivered. Buffer allocation, encoding, durable I/O, and no-op FSM dispatch are included. A second series measures paired native writer submit/complete events, including native event-loop dispatch. Instrumentation has its own overhead.

The first user payload is `buf - 40` bytes and subsequent payloads are `buf - 32`, accounting for entry framing and the initial segment format. Native I/O uses the backend's alignment and can write larger buffers than the encoded entry budget. Snapshotting is disabled for the supported workload range.

The first submission is measured. If more submissions remain, the runner pauses until the initial extra prepare-pool files appear, then continues sequentially. The pause contributes no individual latency sample. A timeout or signal closes/drains the accepted request and makes the run fail; command acceptance is not withdrawn by that timeout.

## JSON Report

Successful submit output has two benchmarks, `submit:<buf>` and `submit:write:<buf>`, each containing a `latency` metric:

```json
{
  "submit:2048": {
    "latency": {"value": 30000, "lower_bound": 20000, "upper_bound": 50000}
  },
  "submit:write:2048": {
    "latency": {"value": 25000, "lower_bound": 15000, "upper_bound": 40000}
  }
}
```

These numbers illustrate the schema. Actual values are nanoseconds. `value` is the nearest-rank 50th percentile, `lower_bound` the observed minimum, and `upper_bound` the observed maximum. Standard vectors retain samples and standard algorithms calculate the statistics, using memory proportional to sample count. Samples are neither rounded into fixed histogram buckets nor clipped to a fixed duration ceiling. JSON numeric formatting uses the classic locale.

Treat stdout as a completed report only when the exit status is zero. Input, initialization, I/O, instrumentation, timeout, interruption, cleanup, or report-output failure returns nonzero. Errors are written to stderr. A failed run does not intentionally emit a partial measurement; an output-device failure can leave incomplete output. Native operations and library-owned buffers are drained before temporary data and event-loop state are released.

## Disk Command

```sh
./build/tools/benchmark/raftpp-benchmark disk --dir /tmp --buf 4096 --size 8M --timeout 30000
```

`disk` measures sequential durable writes through Linux io_uring at queue depth one. It opens the destination with `O_DIRECT`, registers a fixed file/buffer, and submits `IORING_OP_WRITE_FIXED`, `IOSQE_FIXED_FILE`, and `RWF_DSYNC`. No liburing dependency is required. Missing build-time `linux/io_uring.h`, kernel/permission restrictions, or unsupported filesystem operations return errors and a nonzero exit status; there is no substitution with another I/O backend.

| Option | Contract |
| --- | --- |
| `--dir PATH`, `-d PATH` | Existing directory or writable block device; default `.`. Directories receive a private temporary file. Selecting a block device overwrites its first `size` bytes. |
| `--buf BYTES`, `-b BYTES` | Power of two from 512 through 1048576 bytes; default 4096. Must also satisfy the destination's direct-I/O alignment requirements. |
| `--size BYTES`, `-s BYTES` | Positive, at most UINT32_MAX and divisible by buf; default 8 MiB. Cannot exceed block-device capacity. |
| `--timeout MS` | Deadline for ring setup/warm-up/measured writes after file preparation; default 30000 ms. |
| `--help`, `-?` | Show command usage. |

Numbers/suffixes and short/long option value forms match submit. An existing regular file cannot be passed directly as `--dir`. Temporary files are preallocated, filled with aligned zero data, and fsynced along with their containing directory. Preparation is outside throughput/latency measurements. Raw block devices are never preallocated/truncated or removed. The aligned actual write buffer contains the repeated byte pattern `i % 128`.

The first warm-up at offset zero is excluded from individual latency samples. Measured writes then start at offset zero and run exactly `size / buf` times, checking that every completion reports all buf bytes. Latency covers the interval before SQE submission until its matching CQE is observed, in nanoseconds. Throughput divides `size / 1048576` MiB by total seconds including ring setup, warm-up, the measured loop, and ring teardown. Fractional MiB are preserved. Instrumentation and completion-polling overhead are included.

Successful output has one `disk:<buf>` entry containing `latency` and `throughput`. Latency value/lower_bound/upper_bound represent p50/min/max as for submit. Throughput value is MiB/s and both bounds are zero. These example numbers only illustrate the schema:

```json
{
  "disk:4096": {
    "latency": {"value": 30000, "lower_bound": 20000, "upper_bound": 50000},
    "throughput": {"value": 100.5, "lower_bound": 0, "upper_bound": 0}
  }
}
```

Timeout/SIGINT/SIGTERM stop further writes and drain accepted I/O before returning failure. The deadline does not revoke a device's accepted writes or guarantee immediate forced termination. Rings/registered buffers/FDs/temp data remain alive until actual completion, so draining a failed device can exceed the deadline. Completion errors, short writes, mismatched tokens, and cleanup failures do not produce a successful report.

Shared-ring memory/completion behavior and registered-buffer release follow Linux [io_uring(7)](https://man7.org/linux/man-pages/man7/io_uring.7.html) and [io_uring_register(2)](https://man7.org/linux/man-pages/man2/io_uring_register.2.html).

## Kernel Perf and Trace

```sh
./build/tools/benchmark/raftpp-benchmark disk --dir /mnt/nvme --buf 4096 --size 8M --perf
./build/tools/benchmark/raftpp-benchmark disk --dir /tmp --size 8M --trace block,nvme
```

| Option | Contract |
| --- | --- |
| `--perf`, `-p` | Reports actual block/NVMe command latency instead of end-to-end disk latency/throughput. Requires an identified NVMe or null-block device, its tracepoints, and perf permissions. |
| `--trace SYSTEMS`, `-t SYSTEMS` | Enables comma-separated tracefs subsystems during measurement. May be repeated; names must be unique combinations of ASCII letters, digits, underscores, and hyphens. |
| `--tracefs PATH` | Existing tracefs mount path, default `/sys/kernel/tracing`. Event metadata and tracing controls are located under this path. |

`--perf` is available in builds with `linux/perf_event.h`. UID alone does not guarantee access; actual kernel perf/tracefs permissions apply. Missing headers, metadata, or permissions return errors and a nonzero status. Generic/unidentified filesystems do not return an empty successful kernel report.

Per-CPU perf descriptors collect block bio-queue/request-complete/bio-complete and NVMe setup/complete tracepoints. Noncontiguous online CPU IDs are supported; events for each CPU share one writable mmap output ring. Field offsets/sizes come from the running kernel's event `format`, without assuming fixed C struct layouts. Measurement begins after warm-up and stops after measured writes. Rings are consumed after each write and at stop.

Checks ring-wrap boundaries, record lengths, and raw payloads before correlating by global monotonic timestamp. Distinguishes kernel device numbers and partition sectors; NVMe LBAs convert to 512-byte sectors using the actual logical block size. NVMe keys include controller/queue/command IDs and permit reuse after completion. Block and NVMe latencies come from independent records.

Successful `--perf` output contains latency for `disk:block:<buf>` and, for NVMe, additional latency for `disk:nvme:<buf>`. Units and p50/min/max contracts match earlier latency metrics. This output excludes end-to-end throughput. Lost/overflowed/malformed/incomplete records, multiplexing, completion errors, missing events, and ambiguous correlations fail report generation. Voluntary context switches during measurement are also disallowed. Each caller write must remain unsplit/unmerged and generate one traceable block operation and NVMe operation. Submissions from a different I/O worker or device splitting/merging that prevents this correspondence do not produce successful measurements.

`--trace` preserves individual event-enable states and `tracing_on`, enabling the requested subsystems as needed. Normal completion and exception cleanup restore original values, including mixed enable states. It never clears trace contents; read records from `trace` under the selected mount. Other tracers must not concurrently change that mount's configuration. Normal-stop restoration failures are reported as errors; exception cleanup also attempts restoration. Trace-only runs retain ordinary disk latency/throughput output, including instrumentation overhead. Perf FDs and mmap regions close after measurement stops and records are consumed.

Record/access contracts follow Linux [perf ring buffer](https://docs.kernel.org/userspace-api/perf_ring_buffer.html), [perf_event_open(2)](https://man7.org/linux/man-pages/man2/perf_event_open.2.html), and [event tracing](https://docs.kernel.org/trace/events.html).

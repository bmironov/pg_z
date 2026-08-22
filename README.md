# PostgreSQL extension pg_z

<!-- toc -->

- [Highlights of `pg_z` extension](#highlights-of-pg_z-extension)
    * [Zero-Copy Architecture](#zero-copy-architecture)
    * [Decompression Bomb Protection and Size Estimation](#decompression-bomb-protection-and-size-estimation)
    * [PostgreSQL-Integrated Memory Management & Parallel Safety](#postgresql-integrated-memory-management--parallel-safety)
    * [Static Huge Pages Support](#static-huge-pages-support)
    * [Tuple-Scoped Context Lifecycle](#tuple-scoped-context-lifecycle)
- [Data-Flow with `pg_z`](#data-flow-with-pg_z)
- [Requirements and Configuration](#requirements-and-configuration)
- [Database Parameters](#database-parameters)
    * [`pg_z.max_size`](#pg_zmax_size)
    * [`pg_z.mem_chunk_size`](#pg_zmem_chunk_size)
- [Functions Provided by This Extension](#functions-provided-by-this-extension)
- [Usage of PostgreSQL v18+ Ability to Install Extensions Without `sudo`](#usage-of-postgresql-v18-ability-to-install-extensions-without-sudo)
- [Compiling the Extension with Debug Information](#compiling-the-extension-with-debug-information)
- [Supplied Unit Tests for the `pg_z` Functions](#supplied-unit-tests-for-the-pg_z-functions)
- [Supplied Benchmarks for the `pg_z` Functions](#supplied-benchmarks-for-the-pg_z-functions)
    * [Running the Benchmarks](#running-the-benchmarks)
    * [Benchmark Test Dataset Characteristics](#benchmark-test-dataset-characteristics)
    * [Sample Benchmark Test Execution Output](#sample-benchmark-test-execution-output)
- [Supplied Load-Test for the `pg_z` Compression Functions](#supplied-load-test-for-the-pg_z-compression-functions)
    * [Running the Load Test](#running-the-load-test)
    * [Load Test Dataset Characteristics](#load-test-dataset-characteristics)
    * [Sample Load Test Execution Output](#sample-load-test-execution-output)
- [Preparing Static Huge Memory Pages (HMP) on the System](#preparing-static-huge-memory-pages-hmp-on-the-system)
- [How to Pronounce `pg_z`](#how-to-pronounce-pg_z)

<!-- tocstop -->

The development of this extension was inspired by Paul Ramsey’s [`pgsql‑gzip`][1]
project.

Our use case involves processing a large number of huge XML files. To save
network bandwidth, we compress the data in transit. However, storing gzipped
data in PostgreSQL’s TOAST storage becomes inefficient, as TOAST may apply its
own compression layer.

The original `pgsql‑gzip` solution does not fully meet our requirements. We aim
to use modern compression algorithms (`LZ4`, `Zstandard`) to minimize the CPU
load during data retrieval. This approach provides a better storage efficiency
at the cost of slightly higher CPU usage during compression.

## Highlights of `pg_z` extension

### Zero-Copy Architecture

The extension uses a zero-copy methodology where results accumulate directly
within a single memory region. This eliminates the need to copy data to another
buffer before returning it to the requester. This approach provides significant
performance benefits when processing multi-megabyte documents such as log
files, JSON, or XML.

Brotli decompression is an exception here because compressed streams lack
upfront uncompressed size information and require a consistent dictionary
window for back-references. To ensure stability and prevent memory corruption,
the engine pairs a best-guess initial allocation with optimized streaming
through a temporary buffer. This approach compromises pure zero-copy design for
Brotli to guarantee memory safety and predictable performance.

### Decompression Bomb Protection and Size Estimation

Extra care is taken to prevent "decompression bombs" in the `gunzip` function.
Because the Gzip standard does not embed the original data size, standard
implementations are prone to either memory under-allocation or massive
over-allocation for highly compressed payloads. To solve this, `gunzip`
processes data using a `do-while` loop combined with dynamic `repalloc` calls.

Additionally, both `gzip` and `gunzip` employ rough initial size estimates.
This optimization prevents frequent memory reallocations for large documents —
a common performance bottleneck when a small, static chunk size is used from
the start. The implementation carefully balances this initial chunk size to
ensure high performance for both small and large documents.

### PostgreSQL-Integrated Memory Management & Parallel Safety

All supported compression algorithms (`brotli`, `gzip`, `lz4`, `snappy`, and
`zstd`) leverage custom allocators tied directly into the PostgreSQL memory
manager. This architecture prevents memory leaks and enables specialized
allocation optimizations.

Thanks to isolating multi-threaded Zstd workspaces to standard system heap
allocations (`malloc`), background threads run completely independently of
PostgreSQL's single-threaded context tracking. As a result, **every single**
compression and decompression function in this extension is strictly
**`PARALLEL SAFE`** and can be seamlessly leveraged inside parallel
PostgreSQL query execution plans.

### Static Huge Pages Support

The extension's custom memory manager supports the allocation of Static Huge
Pages. This dramatically boosts performance for large documents by allocating
memory in 2 MB chunks instead of standard 4 KB pages, significantly reducing
TLB cache misses.

### Tuple-Scoped Context Lifecycle

The custom [Memory Manager][6] is attached to **`CurrentMemoryContext`**, which
lives only for the duration of processing a single tuple. Once the tuple is
processed, all memory allocated by the extension's functions is automatically
freed. This approach is highly resource-efficient compared to attaching
allocations to the **Transaction Context**, where a single transaction
processing millions of tuples would otherwise cause massive memory bloat.

## Data-Flow with `pg_z`

The documentation in [DATA_FLOW.md][5] outlines the data flow and database
environment necessary for processing large documents. It provides both a visual
diagram and a configuration example to support this functionality.

## Requirements and Configuration

The `pg_z` extension compiles into a hybrid-linked `.so` shared library. To
support the full set of compression algorithms, the following libraries and
their development headers should be installed on the build system:

- `brotli`;
- `zlib` and/or `zlib-ng` (for `gzip` and `deflate` algorithms);
- `lz4`;
- `snappy`;
- `zstd`.

All libraries are completely optional. The `./configure` script automatically
detects what is available in the system, gracefully disables missing components
without failing the build, and declares only the SQL functions for the
algorithms supported by your system.

For deployments that require maximum throughput, `pg_z` supports the high-
performance `zlib-ng` library. It utilizes modern CPU SIMD vectorization and
hardware-accelerated instructions to drastically speed up `gzip` and `deflate`
processing. See details in [CONFIGURE.md][12].

The extension also provides a built-in diagnostic function `pg_z_version()`.
It dynamically queries the compiled `pg_z.so` binary at runtime to report the
exact set of active userspace compression engines available on your specific
PostgreSQL instance. See details in [USAGE.md][4].

To automatically detect your environment and generate the build infrastructure,
simply run:

```bash
autoreconf -if
./configure
```

Detailed compilation tuning and Kubernetes [CloudNativePG][13] (`cnpg`)
deployment steps can be found in [CONFIGURE.md][12].

## Database Parameters

### `pg_z.max_size`

This extension introduces the database parameter `pg_z.max_size`, which allows
you to set a limit on the maximum uncompressed data size in bytes. This is
helpful for protecting the system from processing abnormally large data chunks.
Note that PostgreSQL also has a hard limit on the size of a TOASTed value,
which is set to ~1 GB (2^30 - 1 bytes). Multiple factors affect this, such as:

- TOAST's internal compression (controlled by the `default_toast_compression`
parameter);
- Double compression (storing already compressed data within a compressed TOAST
column);
- The efficiency of the original data compression achieved by this extension's
functions;
- etc.

By default, this value is set to `256MB`, which is more than enough for most
use cases. You can adjust it to any value suitable for your needs. All
functions in this extension check the size of incoming data before compression
or the size of the decompressed value. For compression functions, execution will
abort before any data processing if the input data is too large.
For decompression functions, processing will begin, but if the decompressed
data size exceeds the limit at any point, execution will abort and any
partially decompressed data will be discarded.

You can set this parameter to `pg_z.max_size = 0` to basically disable the
functionality of this extension.

### `pg_z.mem_chunk_size`

This extension introduces the database parameter `pg_z.mem_chunk_size`, which
allows you to set the standard size of the memory chunk by which memory is
allocated and later released for compression and decompression functions. Using
this uniform size minimizes memory fragmentation. It is recommended to set this
value to the most common size of processed documents in the system.

The provided value is always rounded up to a multiple of 8 KB to minimize
memory fragmentation. The upper limit for this parameter is `1GB`, which is
equal to the maximum size of `TEXT` or `BYTEA`

## Functions Provided by This Extension

The `pg_z` extension provides a comprehensive set of functions for working
with compressed data. All of these functions operate entirely in userspace,
allowing you to compress and decompress data on the fly using standard
algorithms.

The table below categorizes all available functions by their underlying
compression algorithms, arranged in alphabetical order:

| Algorithm | Compression Function | Decompression Function |
| :-------- | :------------------- | :---------------------- |
| [Brotli][7] | `brotli` | `unbrotli` |
| [Deflate][8] | `deflate` | `inflate` |
| [Gzip][8] | `gzip` | `gunzip` (aka `ungzip`) |
| Deflate ([zlib-ng][14]) | `deflate_ng` | `inflate_ng` |
| Gzip ([zlib-ng][14]) | `gzip_ng` | `gunzip_ng` (aka `ungzip_ng`) |
| [LZ4][9] | `lz4` | `unlz4` |
| [Snappy][10] | `snappy` | `unsnappy` |
| [Zstandard][11] | `zstd` | `unzstd` |

Detailed definitions and usage examples for these functions can be found in
[USAGE.md][4].

## Usage of PostgreSQL v18+ Ability to Install Extensions Without `sudo`

By default, the `Makefile` will install the extension into the default destination
directory defined by the PostgreSQL parameter `extension_control_path`. Please
note that `make` can produce a precompiled version of the extension in the form
of `.bc` files. These are used by JIT. The common way to install the extension
requires `sudo` privileges:

```bash
sudo make install
```

With PostgreSQL v18, it is possible to install the extension into an arbitrary
directory that doesn't require `sudo` (e.g., `$HOME`). In this case, you can
use the following command:

```bash
DESTDIR=${HOME} make install
```

Such a call will create all necessary subdirectories under your `$HOME` directory.
In order to use this extension, you don't even have to restart the DB; all that
is necessary is to update two parameters as follows (assuming that `$HOME` is
`/var/lib/postgres`):

```sql
ALTER SYSTEM SET extension_control_path='$system:/var/lib/postgresql/usr/share/postgresql/18';
ALTER SYSTEM SET dynamic_library_path='$libdir:/var/lib/postgresql/usr/lib/postgresql/18/lib';
SELECT pg_reload_conf();
```

This is quite handy when running PostgreSQL under the modern `cnpg`
([CloudNativePG][2]) Kubernetes operator.

## Compiling the Extension with Debug Information

The `Makefile` provided with this extension includes a special `debug` target to
compile the source code while preserving all necessary debugging symbols.
Simply run:

```bash
make debug
```

## Supplied Unit Tests for the `pg_z` Functions

According to the standard for an [extension's Makefile][3] there `installcheck`
target executes the set of unit tests supplied with the extension. Run it as
follows:

```bash
make clean
make
make install
make installcheck
```

Last command will execute the `.sql` scripts from the `sql` directory defined
via the `REGRESS` variable in the `Makefile`. Then, it will compare their
output with the corresponding samples (`.out` files in the `expected` directory).
In case of successful unit test run, output should look similar to this:

```text
# +++ regress install-check in  +++
# using postmaster on Unix socket, default port
ok 1         - core                                       15 ms
ok 2         - db_params                                  12 ms
ok 3         - brotli                                     49 ms
ok 4         - gzip                                       67 ms
ok 5         - deflate                                    62 ms
ok 6         - gzip_ng                                    30 ms
ok 7         - deflate_ng                                 29 ms
ok 8         - lz4                                        27 ms
ok 9         - snappy                                     23 ms
ok 10        - zstd                                       38 ms
1..10
# All 10 tests passed.
```

Since the tests are similar in nature, the difference in execution time between
the supplied compression and decompression algorithms is quite visible and
directly points to the performance of these algorithms.

## Supplied Benchmarks for the `pg_z` Functions

The extension includes a built-in benchmarking suite to evaluate the performance
of the supported compression and decompression algorithms directly inside your
PostgreSQL instance.

### Running the Benchmarks

To execute the full benchmarking suite against the active algorithms configured
during setup, run the following command from the root directory:

```bash
make clean
make
make benchmark
```

The script automatically prepares the test environment, executes compression and
decompression routines, tracks execution duration in milliseconds, and securely
tears down the benchmark database objects afterward.

### Benchmark Test Dataset Characteristics

The suite generates a highly compressible temporary dataset to closely simulate
structured database logs with repetitive patterns:

- **Data Structure:** A continuous text block aggregated from 200,000 log
  records.
- **Record Profile:** Each line spans ~571 bytes and contains repeating log
  metadata.
- **Overall Size:** Approximately ~110 MB of uncompressed raw text data.

### Sample Benchmark Test Execution Output

All tests are executed against the same dataset using the default compression
level for each algorithm. Each individual test runs in three steps:

- On-the-fly compression of the dataset (marked as "compress");
- Database table column update with the compression result (not shown below);
- On-the-fly decompression of the compressed column (marked as "decompress").

This architecture allows you to compare different algorithms under distinct
operational loads. The table displays the output size in bytes and the total
execution duration.

Below is a typical TAP-compliant output example demonstrating performance
metrics:

```text
Prepared following test data set:
 dataset_size_bytes | dataset_size_pretty
--------------------+---------------------
  115,088,895       | 110 MB
(1 row)

--------------+-----------------------+--------------+-----------
Test #/Result | Algorithm / Function  | Result, bytes| Duration
--------------+-----------------------+--------------+-----------
  1 OK        | brotli compress       |      307,976 |    152 ms
  2 OK        | brotli decompress     |  115,088,895 |     85 ms
--------------+-----------------------+--------------+-----------
  3 OK        | gzip compress         |      957,968 |    369 ms
  4 OK        | gzip decompress       |  115,088,895 |    104 ms
--------------+-----------------------+--------------+-----------
  5 OK        | deflate compress      |      957,950 |    358 ms
  6 OK        | deflate decompress    |  115,088,895 |     89 ms
--------------+-----------------------+--------------+-----------
  7 OK        | gzip_ng compress      |      946,663 |    146 ms
  8 OK        | gzip_ng decompress    |  115,088,895 |     91 ms
--------------+-----------------------+--------------+-----------
  9 OK        | deflate_ng compress   |      946,645 |    141 ms
 10 OK        | deflate_ng decompress |  115,088,895 |     86 ms
--------------+-----------------------+--------------+-----------
 11 OK        | lz4 compress          |    1,401,239 |    210 ms
 12 OK        | lz4 decompress        |  115,088,895 |     51 ms
--------------+-----------------------+--------------+-----------
 13 OK        | snappy compress       |    6,712,724 |     70 ms
 14 OK        | snappy decompress     |  115,088,895 |     60 ms
--------------+-----------------------+--------------+-----------
 15 OK        | zstd compress         |      312,369 |    158 ms
 16 OK        | zstd decompress       |  115,088,895 |     50 ms
--------------+-----------------------+--------------+-----------
1..16
# All 16 benchmarks passed.
Cleaning up benchmark environment...
```

Use these results to determine the optimal trade-off between compression speed,
decompression overhead, and CPU resource utilization for your specific workload.

## Supplied Load-Test for the `pg_z` Compression Functions

The extension includes a built-in load-test suite to evaluate the performance
of the supported compression algorithms directly inside your PostgreSQL
instance.

### Running the Load Test

To execute the full load test suite against the active algorithms configured
during setup, run the following command from the root directory:

```bash
make clean
make
make load_test
```

The script automatically prepares the test environment, executes compression
routines, tracks execution duration in milliseconds, and securely
tears down the test database objects afterward.

### Load Test Dataset Characteristics

The Load test uses the same dataset as the Benchmark suite described above.

### Sample Load Test Execution Output

Below is a typical TAP-compliant output example demonstrating performance
metrics:

```text
Prepared following test data set:
 dataset_size_bytes | dataset_size_pretty
--------------------+---------------------
  115,088,895       | 110 MB
(1 row)

--------------+----------------------+--------------+-----------
Test #/Result | Algorithm / Function | Result, bytes| Duration
----brotli single-threaded-----------+--------------+-----------
  1 OK        | brotli-0             |      955,911 |     78 ms
  2 OK        | brotli-3             |      307,977 |    155 ms
  3 OK        | brotli-11            |      298,162 |  8,506 ms
----gzip single-threaded-------------+--------------+-----------
  4 OK        | gzip-1               |      964,208 |    198 ms
  5 OK        | gzip-6               |      957,968 |    400 ms
  6 OK        | gzip-9               |      957,968 |    438 ms
----deflate single-threaded----------+--------------+-----------
  7 OK        | deflate-1            |      964,190 |    175 ms
  8 OK        | deflate-6            |      957,950 |    379 ms
  9 OK        | deflate-9            |      957,950 |    401 ms
----gzip_ng single-threaded----------+--------------+-----------
 10 OK        | gzip_ng-1            |    1,723,419 |     76 ms
 11 OK        | gzip_ng-6            |      946,663 |    144 ms
 12 OK        | gzip_ng-9            |      957,968 |    235 ms
----deflate_ng single-threaded-------+--------------+-----------
 13 OK        | deflate_ng-1         |    1,723,401 |     80 ms
 14 OK        | deflate_ng-6         |      946,645 |    141 ms
 15 OK        | deflate_ng-9         |      957,950 |    230 ms
----lz4 single-threaded--------------+--------------+-----------
 16 OK        | lz4-0                |    1,513,806 |     61 ms
 17 OK        | lz4-5                |    1,401,239 |    210 ms
 18 OK        | lz4-16               |    1,403,227 |  1,238 ms
----snappy single-threaded-----------+--------------+-----------
 19 OK        | snappy               |    6,712,726 |     70 ms
----zstd single-threaded-------------+--------------+-----------
 20 OK        | zstd-1               |      393,722 |     69 ms
 21 OK        | zstd-7               |      313,399 |    149 ms
 22 OK        | zstd-19              |      310,747 | 16,853 ms
----zstd multi-threaded--------------+--------------+-----------
 23 OK        | zstd-1-1             |      319,037 |     71 ms
 24 OK        | zstd-7-1             |      313,533 |    174 ms
 25 OK        | zstd-19-1            |      311,173 | 18,981 ms
 26 OK        | zstd-1-2             |      319,037 |     70 ms
 27 OK        | zstd-7-2             |      313,533 |    160 ms
 28 OK        | zstd-19-2            |      311,173 |  9,845 ms
 29 OK        | zstd-1-4             |      319,037 |     76 ms
 30 OK        | zstd-7-4             |      313,533 |    143 ms
 31 OK        | zstd-19-4            |      311,173 | 10,248 ms
 32 OK        | zstd-1-8             |      319,037 |     81 ms
 33 OK        | zstd-7-8             |      313,533 |    145 ms
 34 OK        | zstd-19-8            |      311,173 | 10,217 ms
--------------+----------------------+--------------+-----------
1..34
# All 34 benchmarks passed.
```

Please note that the benchmark table above was collected on an Intel Core
i5-1335U CPU, which features 2 Performance Cores (P-cores) and 8 Efficient
Cores (E-cores) sharing a tight 12MB L3 cache footprint. Consequently, heavy
computational workloads like multi-threaded Zstd compression hit a hardware
scaling bottleneck beyond 2 threads due to E-core frequency limits and shared
cache starvation. On server-grade hardware with symmetrical cores, performance
is expected to scale linearly with higher thread counts.

Feel free to modify the data generation script or the Bash runner script to
test with your own workload and find the optimal configuration for your specific
data.

To speed up testing, you can run the load test as follows and specify only the
required compression algorithms out of the configured list:

```bash
make load_test "lz4 zstd"
```

## Preparing Static Huge Memory Pages (HMP) on the System

The Memory Manager of this extension can allocate static HMP (usually 2MB on
Linux systems) to optimize performance. They will be taken from the available
pool of such pages for the duration of a single tuple processing and then
returned. Thanks to this, the Memory Manager is not affected by queries running
on multiple partitions. If the system page pool is exhausted or not available
at all, the Memory Manager will allocate the necessary RAM using standard pages
(4kB in size).

To minimize fragmentation, the allocation of static HMP is done by rounding up
the requested size to the nearest full page size.

All memory allocations smaller than the size of a static HMP are done via the
PostgreSQL `palloc_extended` function with the `MCXT_ALLOC_NO_OOM` flag to
prevent fatal errors and gracefully return `NULL`.

Since `pg_z` uses its own set of static HMP, you need to account for that and
allocate extra pages on top of the pages required by PostgreSQL itself. Their
quantity can be calculated roughly as follows:

```text
N = A * D / S,
where
N - number of HMPs
A - number of active parallel compression / decompression calls
D - average size of decompressed document in bytes
S - size of an HMP in bytes
```

Monitoring can be done as follows:

```bash
grep HugePages_Free /proc/mem
```

It is highly recommended to have more than zero free HMPs on such a system.

## How to Pronounce `pg_z`

I'm glad you asked. It is pronounced as "pee-gee-zee" and letter "Z" stands for
a universal reference to compression as in `.Z` file type.

[1]: https://github.com/pramsey/pgsql-gzip
[2]: https://cloudnative-pg.io/
[3]: https://www.postgresql.org/docs/current/extend-pgxs.html
[4]: USAGE.md
[5]: DATA_FLOW.md
[6]: MEMORY_MANAGER.md
[7]: https://github.com/google/brotli
[8]: https://zlib.net
[9]: https://github.com/lz4/lz4
[10]: https://github.com/google/snappy
[11]: https://github.com/facebook/zstd
[12]: CONFIGURE.md
[13]: https://github.com/cloudnative-pg
[14]: https://github.com/zlib-ng/zlib-ng

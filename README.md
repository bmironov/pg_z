# PostgreSQL extension pg_z

<!-- toc -->

- [Requirements for the `pg_z` Extension](#requirements-for-the-pg_z-extension)
- [Database Parameter `pg_z.max_size`](#database-parameter-pg_zmax_size)
- [Functions Provided by This Extension](#functions-provided-by-this-extension)
- [Usage of PostgreSQL v18+ Ability to Install Extensions Without `sudo`](#usage-of-postgresql-v18-ability-to-install-extensions-without-sudo)
- [Compiling the Extension with Debug Information](#compiling-the-extension-with-debug-information)
- [Supplied Unit Tests for `pg_z` Functions](#supplied-unit-tests-for-pg_z-functions)
- [Highlights of `pg_z` extension](#highlights-of-pg_z-extension)
    * [Zero-Copy Architecture](#zero-copy-architecture)
    * [Decompression Bomb Protection and Size Estimation](#decompression-bomb-protection-and-size-estimation)
    * [PostgreSQL-Integrated Memory Management & Parallel Safety](#postgresql-integrated-memory-management--parallel-safety)
    * [Static Huge Pages Support](#static-huge-pages-support)
    * [Tuple-Scoped Context Lifecycle](#tuple-scoped-context-lifecycle)
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


## Requirements for the `pg_z` Extension

The `pg_z` extension requires three libraries and their development headers to
be installed on the system to compile into a `.so` file:
* `zlib` (to support the `gzip` and `deflate` algorithms);
* `lz4` (to support `LZ4`);
* `zstd` (to support `Zstandard`).

Once all required libraries and their development headers are installed, run
the following commands to generate the build scripts and configure the
extension:

```bash
autoreconf -if
./configure
```

Linking is done dynamically, so these libraries must be installed on every
system where the extension runs. Running `./configure` will automatically
detect the available libraries and generate the corresponding `pg_z--*.sql`
file, ensuring that the subsequent `CREATE EXTENSION` command declares only
the functions supported by your system.

If you want to install this extension as an "Extension Image" for the
CloudNativePG (`cnpg`) operator for Kubernetes, please refer to the provided
`Dockerfile` and `cluster.yaml` templates in this project. The `Dockerfile`
builds an image containing all the necessary files to deploy the `pg_z`
extension along with the required system libraries. This image can then be
attached to a vanilla PostgreSQL v18+ instance running under `cnpg`.


## Database Parameter `pg_z.max_size`

This extension introduces the database parameter `pg_z.max_size`, which allows
you to set a limit on the maximum uncompressed data size in bytes. This is
helpful for protecting the system from processing abnormally large data chunks.
Note that PostgreSQL also has a hard limit on the size of a TOASTed value,
which is set to ~1 GB (2^30 - 1 bytes). Multiple factors affect this, such as:
* TOAST's internal compression (controlled by the `default_toast_compression`
parameter);
* Double compression (storing already compressed data within a compressed TOAST
column);
* The efficiency of the original data compression achieved by this extension's
functions;
* etc.

By default, this value is set to `256MB`, which is more than enough for most
use cases. You can adjust it to any value suitable for your needs. All
functions in this extension check the size of incoming data before compression
or the size of the decompressed value. For compression functions, execution will
abort before any data processing if the input data is too large.
For decompression functions, processing will begin, but if the decompressed
data size exceeds the limit at any point, execution will abort and any
partially decompressed data will be discarded.

To disable the uncompressed data size limit check, set `pg_z.max_size = -1`.


## Functions Provided by This Extension

The `pg_z` extension provides several functions for working with compressed
data. These functions are categorized into groups based on their underlying
compression algorithms:

* gzip
    * gzip
    * gunzip (aka ungzip)
* deflate
    * deflate
    * inflate
* LZ4
    * lz4
    * unlz4
* Zstandard
    * zstd
    * unzstd

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


## Supplied Unit Tests for `pg_z` Functions

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
In case of successful unit test run, output should look similar this:

```
# +++ regress install-check in  +++
# using postmaster on Unix socket, default port
ok 1         - gzip                                       73 ms
ok 2         - deflate                                    57 ms
ok 3         - lz4                                        26 ms
ok 4         - zstd                                       33 ms
1..4
# All 4 tests passed
```

Since the tests are similar in nature, the difference in execution time between
the supplied compression and decompression algorithms is quite visible and
directly points to the performance of these algorithms.


## Highlights of `pg_z` extension

### Zero-Copy Architecture
The extension uses a zero-copy methodology where results are accumulated
directly within a single memory region. This eliminates the need to copy data
to another buffer before returning it to the requester. This approach provides
significant performance benefits when processing multi-megabyte documents such
as log files, JSON, or XML.

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
All supported compression algorithms (`gzip`, `lz4`, and `zstd`) leverage
custom allocators tied directly into the PostgreSQL memory manager. This
architecture prevents memory leaks and enables specialized allocation
optimizations.

As a result, `gzip` and `lz4` functions are safely marked as
**`PARALLEL SAFE`**. However, because `zstd` manages its own internal threads
outside of PostgreSQL's control, `zstd`-related functions are marked as
**`PARALLEL UNSAFE`**.

### Static Huge Pages Support
The extension's custom memory manager supports the allocation of Static Huge
Pages. This dramatically boosts performance for large documents by allocating
memory in 2 MB chunks instead of standard 4 KB pages, significantly reducing
TLB cache misses.

### Tuple-Scoped Context Lifecycle
The custom memory manager is attached to **`CurrentMemoryContext`**, which
lives only for the duration of processing a single tuple. Once the tuple is
processed, all memory allocated by the extension's functions is automatically
freed. This approach is highly resource-efficient compared to attaching
allocations to the **Transaction Context**, where a single transaction
processing millions of tuples would otherwise cause massive memory bloat.


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

```
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

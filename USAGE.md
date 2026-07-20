# Compression and Decompression Functions

<!-- toc -->

- [Gzip and Deflate Algorithms (zlib)](#gzip-and-deflate-algorithms-zlib)
    * [`deflate`](#deflate)
        + [Description](#description)
        + [Usage Notes](#usage-notes)
        + [Examples](#examples)
    * [`inflate`](#inflate)
        + [Description](#description-1)
        + [Usage Notes](#usage-notes-1)
        + [Examples](#examples-1)
    * [`gzip`](#gzip)
        + [Description](#description-2)
        + [Usage Notes](#usage-notes-2)
        + [Examples](#examples-2)
    * [`gunzip`](#gunzip)
        + [Description](#description-3)
        + [Usage Notes](#usage-notes-3)
        + [Examples](#examples-3)
- [LZ4 Algorithm](#lz4-algorithm)
    * [`lz4`](#lz4)
        + [Description](#description-4)
        + [Usage Notes](#usage-notes-4)
        + [Examples](#examples-4)
    * [`unlz4`](#unlz4)
        + [Description](#description-5)
        + [Usage Notes](#usage-notes-5)
        + [Examples](#examples-5)
- [Zstandard Algorithm (zstd)](#zstandard-algorithm-zstd)
    * [Important Execution Safety Note](#important-execution-safety-note)
    * [`zstd`](#zstd)
        + [Description](#description-6)
        + [Usage Notes](#usage-notes-6)
        + [Examples](#examples-6)
    * [`unzstd`](#unzstd)
        + [Description](#description-7)
        + [Usage Notes](#usage-notes-7)
        + [Examples](#examples-7)

<!-- tocstop -->

The `pg_z` extension provides a collection of functions to compress and
decompress data within PostgreSQL using standard industry algorithms:
Gzip/Deflate (zlib), LZ4, and Zstandard (zstd).

Compression functions accept both `bytea` and `text` data types. Decompression
functions strictly take `bytea` inputs and return the raw decompressed `bytea`
stream.


## Gzip and Deflate Algorithms (zlib)

These functions utilize the standard `zlib` library. `deflate` processes raw
compressed streams, while `gzip` wraps the compressed data inside a standard
gzip file structure wrapper including headers and trailers.

All functions in this section are marked as `PARALLEL SAFE` and `IMMUTABLE`.

### `deflate`

deflate ( uncompressed bytea \[, compression_level integer \] ) → bytea

deflate ( uncompressed text \[, compression_level integer \] ) → bytea

#### Description

Compresses the input data using the raw zlib deflate format specified in
RFC 1951.

#### Usage Notes

The optional `compression_level` parameter must be an integer within the range
of **`-1` to `9`**.
* `-1` utilizes the default compression level balanced between speed and size
(typically equivalent to level 6).
* `0` provides no compression (data is only stored).
* `1` provides the fastest compression speed.
* `9` provides the maximum compression ratio at the cost of execution time and
higher CPU usage.

#### Examples

```sql
SELECT deflate('hello world');
-- Result: \xcb48cdc9c95728cf2fca490100

SELECT deflate('compress me'::bytea, 9);
-- Result: \x4bcecf2d284a2d2e56c84d0500
```

***

### `inflate`

inflate ( compressed bytea ) → bytea

#### Description

Decompresses a raw deflate byte stream complying with RFC 1951 back into its
original binary layout.

#### Usage Notes

This function will fail with an error if the input byte sequence is corrupted
or is not a valid raw deflate stream. It cannot parse streams wrapped with gzip
headers.

#### Examples

```sql
SELECT convert_from(inflate(deflate('hello world')), 'UTF8');
-- Result: hello world
```

***

### `gzip`

gzip ( uncompressed bytea \[, compression_level integer \] ) → bytea

gzip ( uncompressed text \[, compression_level integer \] ) → bytea

#### Description

Compresses the input data and wraps it using the standard gzip file format
layout specified in RFC 1952.

#### Usage Notes

The optional `compression_level` parameter accepts integers within the range of
**`-1` to `9`**, defaulting to `-1`. The gzip format includes a header that
makes the output slightly larger than raw deflate for small inputs, but the
resulting binary data is fully compatible with external tools like the
command-line `gunzip` utility.

#### Examples

```sql
SELECT gzip('hello world'::bytea, 9);
-- Result: \x1f8b0800000000000203cb48cdc9c95728cf2fca49010085114a0d0b000000
```

***

### `gunzip` (aka `ungzip`)

gunzip ( compressed bytea ) → bytea

#### Description

Decompresses a gzip-wrapped byte stream complying with RFC 1952 back into its
original binary layout.

`ungzip` is just a synonym for `gunzip` to keep the "un" prefix in front of
the algorithm's name, maintaining consistency with other cases.


#### Usage Notes

The input must be a valid gzip binary payload starting with the appropriate
magic bytes (`\x1f8b`). Attempting to pass a raw deflate stream or any other
compression format will result in a runtime evaluation error.

#### Examples

```sql
SELECT convert_from(gunzip(gzip('hello world')), 'UTF8');
-- Result: hello world
```

---


## LZ4 Algorithm

The LZ4 functions focus on extremely high compression and decompression speeds,
trading a slightly lower compression ratio compared to zlib for minimized CPU
overhead. Data streams comply with the standardized LZ4 frame format
specifications outlined in RFC 8478.

All functions in this section are marked as `PARALLEL SAFE` and `IMMUTABLE`.

### `lz4`

lz4 ( uncompressed bytea \[, compression_level integer \] ) → bytea

lz4 ( uncompressed text \[, compression_level integer \] ) → bytea

#### Description

Compresses the input data using the high-speed LZ4 algorithm framework.

#### Usage Notes

The optional `compression_level` parameter accepts integers within the range of
**`0` to `16`** (with standard implementations supporting up to level 12 or 16
depending on the underlying `liblz4` high-compression variants).
* The default value is `5`.
* Higher values enable the HC (High Compression) mode, which improves the
compression ratio but significantly increases compression time. Decompression
speed remains uniformly fast regardless of the compression level.

#### Examples

```sql
SELECT lz4('hello world');
-- Result: \x04224d1848700b00000000000000d70b00008068656c6c6f20776f726c6400000000
```

***

### `unlz4`

unlz4 ( compressed bytea ) → bytea

#### Description

Decompresses an LZ4 compressed byte stream back into its original binary layout
according to RFC 8478.

#### Usage Notes

Input sequences must represent valid payloads compressed strictly via the
matching `lz4()` implementation functions.

#### Examples

```sql
SELECT convert_from(unlz4(lz4('hello world')), 'UTF8');
-- Result: hello world
```

---

## Zstandard Algorithm (zstd)

Zstandard provides real-time compression scenarios with scaling ratios
comparable to the best archive formats, natively specified in RFC 8878.

### Important Execution Safety Note

Unlike the previous algorithms, the `zstd` and `unzstd` functions are explicitly
designated as **`PARALLEL UNSAFE`**. The underlying C implementation natively
manages its own operating system worker threads when requested. Marking these
functions as `PARALLEL UNSAFE` forces PostgreSQL to retain execution inside a
single query worker model, preventing conflict between the PostgreSQL parallel
layer and the internal multithreading logic of the Zstandard library.

### `zstd`

zstd ( uncompressed bytea \[, compression_level integer \[, threads integer \] \] ) → bytea

zstd ( uncompressed text \[, compression_level integer \[, threads integer \] \] ) → bytea

#### Description

Compresses the input data using the Zstandard (zstd) algorithm wrapper framework.

#### Usage Notes

* The optional `compression_level` parameter accepts integers within the range of
**`1` to `22`** (with standard levels going up to 19, and levels 20-22 acting
as ultra-high memory modes). It defaults to `7`.
* The optional `threads` parameter sets the number of concurrent worker threads
spawned internally by the `zstd` engine to process the specific chunk,
defaulting to `1`. Setting `threads > 1` can significantly reduce compression
times for large text or binary payloads.

#### Examples

```sql
SELECT zstd('hello world', 7, 2);
-- Result: \x28b52ffd200b59000068656c6c6f20776f726c64
```

***

### `unzstd`

unzstd ( compressed bytea ) → bytea

#### Description

Decompresses a Zstandard compressed byte stream complying with RFC 8878 back
into its original binary layout.

#### Usage Notes

The function evaluates the incoming binary blocks. If structural blocks or
checksum bounds do not match valid Zstandard specifications, execution is
terminated with an explicit engine error.

#### Examples

```sql
SELECT convert_from(unzstd(zstd('zstd multi-threaded output', 12, 4)), 'UTF8');
-- Result: zstd multi-threaded output
```


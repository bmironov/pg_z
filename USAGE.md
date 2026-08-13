# Compression and Decompression Functions

<!-- toc -->

- [Brotli Algorithm](#brotli-algorithm)
    * [`brotli`](#brotli)
        + [`brotli` Description](#brotli-description)
        + [`brotli` Usage Notes](#brotli-usage-notes)
        + [`brotli` Examples](#brotli-examples)
    * [`unbrotli`](#unbrotli)
        + [`unbrotli` Usage Notes](#unbrotli-usage-notes)
        + [`unbrotli` Examples](#unbrotli-examples)
- [Gzip and Deflate Algorithms (zlib)](#gzip-and-deflate-algorithms-zlib)
    * [`deflate`](#deflate)
        + [`deflate` Description](#deflate-description)
        + [`deflate` Usage Notes](#deflate-usage-notes)
        + [`deflate` Examples](#deflate-examples)
    * [`inflate`](#inflate)
        + [`inflate` Description](#inflate-description)
        + [`inflate` Usage Notes](#inflate-usage-notes)
        + [`inflate` Examples](#inflate-examples)
    * [`gzip`](#gzip)
        + [`gzip` Description](#gzip-description)
        + [`gzip` Usage Notes](#gzip-usage-notes)
        + [`gzip` Examples](#gzip-examples)
    * [`gunzip` (aka `ungzip`)](#gunzip-aka-ungzip)
        + [`gunzip` Description](#gunzip-description)
        + [`gunzip` Usage Notes](#gunzip-usage-notes)
        + [`gunzip` Examples](#gunzip-examples)
- [LZ4 Algorithm](#lz4-algorithm)
    * [`lz4`](#lz4)
        + [`lz4` Description](#lz4-description)
        + [`lz4` Usage Notes](#lz4-usage-notes)
        + [`lz4` Examples](#lz4-examples)
    * [`unlz4`](#unlz4)
        + [`unlz4` Description](#unlz4-description)
        + [`unlz4` Usage Notes](#unlz4-usage-notes)
        + [`unlz4` Examples](#unlz4-examples)
- [Snappy Algorithms](#snappy-algorithms)
    * [`snappy`](#snappy)
        + [`snappy` Description](#snappy-description)
        + [`snappy` Usage Notes](#snappy-usage-notes)
        + [`snappy` Examples](#snappy-examples)
    * [`unsnappy`](#unsnappy)
        + [`unsnappy` Description](#unsnappy-description)
        + [`unsnappy` Usage Notes](#unsnappy-usage-notes)
        + [`unsnappy` Examples](#unsnappy-examples)
- [Zstandard Algorithm (zstd)](#zstandard-algorithm-zstd)
    * [Zstandard Important Execution Safety Note](#zstandard-important-execution-safety-note)
    * [`zstd`](#zstd)
        + [`zstd` Description](#zstd-description)
        + [`zstd` Usage Notes](#zstd-usage-notes)
        + [`zstd` Examples](#zstd-examples)
    * [`unzstd`](#unzstd)
        + [`unzstd` Description](#unzstd-description)
        + [`unzstd` Usage Notes](#unzstd-usage-notes)
        + [`unzstd` Examples](#unzstd-examples)

<!-- tocstop -->

The `pg_z` extension provides a collection of functions to compress and
decompress data within PostgreSQL using standard industry algorithms:
[Brotli][1], Gzip/Deflate ([zlib][2]), [LZ4][3], [Snappy][4], and
[Zstandard][5] (zstd).

Compression functions accept both `bytea` and `text` data types. Decompression
functions strictly take `bytea` inputs and return the raw decompressed `bytea`
stream.

## Brotli Algorithm

The [Brotli][1] compression algorithm (RFC 7932) is highly optimized for text
data, web content, and JSON payloads. It provides exceptional compression
ratios, outperforming Gzip and often matching or exceeding Zstandard on highly
repetitive string patterns.

All functions in this section are marked as `PARALLEL SAFE` and `IMMUTABLE`.

### `brotli`

```text
brotli ( uncompressed bytea [, compression_level integer ] ) → bytea

brotli ( uncompressed text [, compression_level integer ] ) → bytea
```

#### `brotli` Description

Compresses input raw bytes (`bytea`) or text (`text`) using the Brotli
algorithm with a specified compression quality level.

#### `brotli` Usage Notes

The optional `compression_level` parameter must be an integer within the range
of **`0` to `11`**, with `3` selected as the default value.

- `0–3`: Provides the fastest compression levels (good for real-time transmission
where server CPU is heavily constrained).
- `4–6`: Provides a balance between speed and ratio (good for on-the-fly
compression, recommended for general use).
- `7–9`: Provides a high level of compression (good for on-the-fly compression if
build time is a concern).
- `10–11`: Provides maximum compression (ideal for pre-compressing static assets
like HTML, CSS, and JS).

#### `brotli` Examples

```sql
SELECT brotli('hello world');
-- Result: \x0128000468656c6c6f20776f726c6403

SELECT brotli('compress me'::bytea, 9);
-- Result: \x01280004636f6d7072657373206d6503
```

***

### `unbrotli`

```text
unbrotli ( compressed bytea ) → bytea
```

#### `unbrotli` Usage Notes

Decompresses a Brotli-compressed binary stream back into its original raw bytes
layout.

#### `unbrotli` Examples

```sql
SELECT convert_from(unbrotli(brotli('hello world')), 'UTF8');
-- Result: hello world
```

***

## Gzip and Deflate Algorithms (zlib)

These functions utilize the standard [`zlib`][2] library. `deflate` processes raw
compressed streams, while `gzip` wraps the compressed data inside a standard
gzip file structure wrapper including headers and trailers.

All functions in this section are marked as `PARALLEL SAFE` and `IMMUTABLE`.

### `deflate`

```text
deflate ( uncompressed bytea [, compression_level integer ] ) → bytea

deflate ( uncompressed text [, compression_level integer ] ) → bytea
```

#### `deflate` Description

Compresses the input data using the raw zlib deflate format specified in
RFC 1951.

#### `deflate` Usage Notes

The optional `compression_level` parameter must be an integer within the range
of **`-1` to `9`**.

- `-1` utilizes the default compression level balanced between speed and size
(typically equivalent to level 6).
- `0` provides no compression (data is only stored).
- `1` provides the fastest compression speed.
- `9` provides the maximum compression ratio at the cost of execution time and
higher CPU usage.

#### `deflate` Examples

```sql
SELECT deflate('hello world');
-- Result: \xcb48cdc9c95728cf2fca490100

SELECT deflate('compress me'::bytea, 9);
-- Result: \x4bcecf2d284a2d2e56c84d0500
```

***

### `inflate`

```text
inflate ( compressed bytea ) → bytea
```

#### `inflate` Description

Decompresses a raw deflate byte stream complying with RFC 1951 back into its
original binary layout.

#### `inflate` Usage Notes

This function will fail with an error if the input byte sequence is corrupted
or is not a valid raw deflate stream. It cannot parse streams wrapped with gzip
headers.

#### `inflate` Examples

```sql
SELECT convert_from(inflate(deflate('hello world')), 'UTF8');
-- Result: hello world
```

***

### `gzip`

```text
gzip ( uncompressed bytea [, compression_level integer ] ) → bytea

gzip ( uncompressed text [, compression_level integer ] ) → bytea
```

#### `gzip` Description

Compresses the input data and wraps it using the standard gzip file format
layout specified in RFC 1952.

#### `gzip` Usage Notes

The optional `compression_level` parameter accepts integers within the range of
**`-1` to `9`**, defaulting to `-1`. The gzip format includes a header that
makes the output slightly larger than raw deflate for small inputs, but the
resulting binary data is fully compatible with external tools like the
command-line `gunzip` utility.

#### `gzip` Examples

```sql
SELECT gzip('hello world'::bytea, 9);
-- Result: \x1f8b0800000000000203cb48cdc9c95728cf2fca49010085114a0d0b000000
```

***

### `gunzip` (aka `ungzip`)

```text
gunzip ( compressed bytea ) → bytea

ungzip ( compressed bytea ) → bytea
```

#### `gunzip` Description

Decompresses a gzip-wrapped byte stream complying with RFC 1952 back into its
original binary layout.

`ungzip` is just a synonym for `gunzip` to keep the "un" prefix in front of
the algorithm's name, maintaining consistency with other cases.

#### `gunzip` Usage Notes

The input must be a valid gzip binary payload starting with the appropriate
magic bytes (`\x1f8b`). Attempting to pass a raw deflate stream or any other
compression format will result in a runtime evaluation error.

#### `gunzip` Examples

```sql
SELECT convert_from(gunzip(gzip('hello world')), 'UTF8');
-- Result: hello world
```

***

## LZ4 Algorithm

The [LZ4][3] functions focus on extremely high compression and decompression speeds,
trading a slightly lower compression ratio compared to zlib for minimized CPU
overhead. Data streams comply with the standardized LZ4 frame format
specifications outlined in RFC 8478.

All functions in this section are marked as `PARALLEL SAFE` and `IMMUTABLE`.

### `lz4`

```text
lz4 ( uncompressed bytea [, compression_level integer ] ) → bytea

lz4 ( uncompressed text [, compression_level integer ] ) → bytea
```

#### `lz4` Description

Compresses the input data using the high-speed LZ4 algorithm framework.

#### `lz4` Usage Notes

The optional `compression_level` parameter accepts integers within the range of
**`0` to `16`** (with standard implementations supporting up to level 12 or 16
depending on the underlying `liblz4` high-compression variants).

- The default value is `5`.
- Higher values enable the HC (High Compression) mode, which improves the
compression ratio but significantly increases compression time. Decompression
speed remains uniformly fast regardless of the compression level.

#### `lz4` Examples

```sql
SELECT lz4('hello world');
-- Result: \x04224d1848700b00000000000000d70b00008068656c6c6f20776f726c6400000000
```

***

### `unlz4`

```text
unlz4 ( compressed bytea ) → bytea
```

#### `unlz4` Description

Decompresses an LZ4 compressed byte stream back into its original binary layout
according to RFC 8478.

#### `unlz4` Usage Notes

Input sequences must represent valid payloads compressed strictly via the
matching `lz4()` implementation functions.

#### `unlz4` Examples

```sql
SELECT convert_from(unlz4(lz4('hello world')), 'UTF8');
-- Result: hello world
```

***

## Snappy Algorithms

The [Snappy][4] compression algorithm is designed for maximum compression and
decompression speeds with reasonable compression ratios. It is highly efficient
and optimized for streaming data infrastructures.

All functions in this section are marked as `PARALLEL SAFE` and `IMMUTABLE`.

### `snappy`

```text
snappy ( uncompressed bytea ) → bytea

snappy ( uncompressed text ) → bytea
```

#### `snappy` Description

Compresses input raw bytes (`bytea`) or text (`text`) into the official Snappy
Framing Format using a safe chunk-by-chunk streaming pipeline.

#### `snappy` Usage Notes

The Snappy algorithm intentionally trades compression ratio for pure execution
speed. It does not accept a compression level parameter and operates in a
single, hardware-optimized mode.

- Processes data in independent chunks up to 64KB.
- Automatically generates mandatory stream identifiers and CRC32C checksums.
- Transparently falls back to uncompressed data chunks if a specific data
  block cannot be compressed efficiently, avoiding needless CPU overhead.

#### `snappy` Examples

```sql
SELECT snappy('hello world');
-- Result: \xff060000734e61507059010f0000007ed86d68656c6c6f20776f726c64
```

***

### `unsnappy`

```text
unsnappy ( compressed bytea ) → bytea
```

#### `unsnappy` Description

Decompresses a Snappy-framed binary stream back into its original raw bytes
layout.

#### `unsnappy` Usage Notes

The function processes incoming data stream-by-stream using true zero-copy
memory allocation.

- Safely accommodates custom framing chunks ranging up to 16MB in size.
- Parses stream boundaries dynamically, preventing out-of-memory errors on
  large datasets.
- Guarantees instant protection against compression bomb vectors by verifying
  chunk lengths before expanding the output buffer.

#### `unsnappy` Examples

```sql
SELECT convert_from(unsnappy(snappy('hello world')), 'UTF8');
-- Result: hello world
```

***

## Zstandard Algorithm (zstd)

[Zstandard][5] provides real-time compression scenarios with scaling ratios
comparable to the best archive formats, natively specified in RFC 8878.

### Zstandard Important Execution Safety Note

Unlike the previous algorithms, the `zstd` and `unzstd` functions are explicitly
designated as **`PARALLEL UNSAFE`**. The underlying C implementation natively
manages its own operating system worker threads when requested. Marking these
functions as `PARALLEL UNSAFE` forces PostgreSQL to retain execution inside a
single query worker model, preventing conflict between the PostgreSQL parallel
layer and the internal multithreading logic of the Zstandard library.

### `zstd`

```text
zstd ( uncompressed bytea [, compression_level integer [, threads integer ] ] ) → bytea

zstd ( uncompressed text [, compression_level integer [, threads integer ] ] ) → bytea
```

#### `zstd` Description

Compresses the input data using the Zstandard (zstd) algorithm wrapper framework.

#### `zstd` Usage Notes

- The optional `compression_level` parameter accepts integers within the range of
**`1` to `22`** (with standard levels going up to 19, and levels 20-22 acting
as ultra-high memory modes). It defaults to `7`.
- The optional `threads` parameter sets the number of concurrent worker threads
spawned internally by the `zstd` engine to process the specific chunk,
defaulting to `1`. Setting `threads > 1` can significantly reduce compression
times for large text or binary payloads.

#### `zstd` Examples

```sql
SELECT zstd('hello world', 7, 2);
-- Result: \x28b52ffd200b59000068656c6c6f20776f726c64
```

***

### `unzstd`

```text
unzstd ( compressed bytea ) → bytea
```

#### `unzstd` Description

Decompresses a Zstandard compressed byte stream complying with RFC 8878 back
into its original binary layout.

#### `unzstd` Usage Notes

The function evaluates the incoming binary blocks. If structural blocks or
checksum bounds do not match valid Zstandard specifications, execution is
terminated with an explicit engine error.

#### `unzstd` Examples

```sql
SELECT convert_from(unzstd(zstd('zstd multi-threaded output', 12, 4)), 'UTF8');
-- Result: zstd multi-threaded output
```

[1]: https://github.com/google/brotli
[2]: https://zlib.net
[3]: https://github.com/lz4/lz4
[4]: https://github.com/google/snappy
[5]: https://github.com/facebook/zstd

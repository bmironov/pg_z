# Advanced Configuration and Compilation Guide

<!-- toc -->

- [Feature Customization Flags](#feature-customization-flags)
    * [Toggling Algorithms](#toggling-algorithms)
    * [Controlling Linking Type](#controlling-linking-type)
- [Operating System Specific Constraints](#operating-system-specific-constraints)
    * [Debian and Ubuntu Distributions](#debian-and-ubuntu-distributions)
    * [Package "Build" mode](#package-build-mode)
- [Architectural Comparison: Static vs. Dynamic Linking](#architectural-comparison-static-vs-dynamic-linking)
- [`gzip` / `deflate` Hardware Acceleration with `zlib-ng`](#gzip--deflate-hardware-acceleration-with-zlib-ng)
- [Custom Extreme Optimization (Zero Runtime Dependencies)](#custom-extreme-optimization-zero-runtime-dependencies)
- [Verification: Auditing the Compiled `pg_z.so` Binary](#verification-auditing-the-compiled-pg_zso-binary)
    * [1. Verifying Dynamic Dependencies (`ldd`)](#1-verifying-dynamic-dependencies-ldd)
        + [What to look for in the `ldd` output](#what-to-look-for-in-the-ldd-output)
    * [2. Inspecting Direct Linker Requirements (`readelf`)](#2-inspecting-direct-linker-requirements-readelf)
        + [Ideal Output Example (on Debian with default settings)](#ideal-output-example-on-debian-with-default-settings)
    * [3. Auditing Embedded Static Symbols (`nm`)](#3-auditing-embedded-static-symbols-nm)
        + [What to look for in the `nm` output](#what-to-look-for-in-the-nm-output)
    * [4. Diagnostic and Runtime Verification](#4-diagnostic-and-runtime-verification)
        + [Example Usage of the `pg_z_version`](#example-usage-of-the-pg_z_version)
- [Kubernetes and CloudNativePG (`cnpg`) Deployment](#kubernetes-and-cloudnativepg-cnpg-deployment)

<!-- tocstop -->

This document explains how to configure, customize, and compile the `pg_z`
extension for specific environments, custom library sets, and production
deployments.

## Feature Customization Flags

The `./configure` script provides explicit flags to include or exclude specific
algorithms on demand. If a library is excluded or missing, the generated
`pg_z--*.sql` template will gracefully skip declaring its respective database
functions.

### Toggling Algorithms

- `--without-brotli`  : Exclude Brotli functions from the build.
- `--without-gzip`    : Exclude `zlib`-based Gzip and Deflate functions.
- `--without-gzip-ng` : Exclude `zlib-ng`-based Gzip and Deflate functions.
- `--without-lz4`     : Exclude LZ4 functions.
- `--without-snappy`  : Exclude Snappy functions.
- `--without-zstd`    : Exclude Zstandard functions.

### Controlling Linking Type

You can force specific libraries to link either dynamically (`dynamic`) or
statically (`static`) to suit your target deployment requirements:

- `--with-link-brotli=static|dynamic`  (Default: `static`)
- `--with-link-gzip=static|dynamic`    (Default: `dynamic`)
- `--with-link-gzip-ng=static|dynamic` (Default: `static`)
- `--with-link-lz4=static|dynamic`     (Default: `static`)
- `--with-link-snappy=static|dynamic`  (Default: `dynamic`, `dynamic` only on Debian)
- `--with-link-zstd=static|dynamic`    (Default: `static`)

## Operating System Specific Constraints

### Debian and Ubuntu Distributions

Please note that in Debian/Ubuntu environments, certain libraries cannot be
linked statically out of the box due to upstream packaging policies:

1. **Standard `zlib`**: Debian supplies the static archive `libz.a` compiled
   without the `-fPIC` flag. This breaks shared library creation. Therefore,
   standard `zlib` **must** be linked dynamically on Debian.
2. **Snappy**: `libsnappy` is a C++ library, and standard Debian repositories
   lack a static C-wrapper archive (`libsnappy.a` with `-fPIC`). Thus, Snappy is
   forced to `dynamic` linking only on Debian systems.

The `configure` script automatically detects Debian/Ubuntu systems, adjusts
the default linking behaviors safely, and blocks invalid manual configurations.

### Package "Build" mode

To activate this mode, pass the `--enable-package-build` option to the
`configure` script.

This mode bypasses default OS-specific linking constraints, allowing the
compiler to generate a monolithic `pg_z.so` shared object with all selected
compression libraries linked statically. This provides several production
advantages:

- It generates a self-sufficient extension binary that eliminates runtime
  dependencies on system-level shared libraries (`.so`).
- It allows decoupling the extension from aging upstream distribution packages
  by compiling and embedding newer, optimized versions of the compression
  libraries directly into the runtime binary.

Once `--enable-package-build` is specified, you can explicitly configure the
linking type for each individual library using the corresponding `--with-*-link`
arguments.

## Architectural Comparison: Static vs. Dynamic Linking

The following matrix evaluates the trade-offs between linking mechanisms
specifically for the `pg_z` extension context under Enterprise Linux (RHEL/Rocky
Linux) and Debian/Ubuntu deployments:

| Evaluation Criteria | Dynamic Linking (`-l*`) | Static Linking (`lib*.a`) |
| :--- | :--- | :--- |
| **Execution Hot-Path Performance** | Baseline. Nanosecond overhead via PLT/GOT indirection is completely negligible for a few block-level calls per query. | Marginal micro-optimization. Replaces indirect runtime jumps with direct call instructions. No practical impact on macro-benchmarks. |
| **Upstream Library Versions** | Locked to default OS package manager repositories (e.g., legacy LZ4 v1.9.3 on Rocky 9). | **Bakes in cutting-edge versions** compiled from upstream source (e.g., LZ4 v1.10.0), unlocking internal algorithmic enhancements. |
| **Binary Footprint (`pg_z.so`)** | Minimal (few Kilobytes). Relies on shared memory objects loaded into the OS runtime address space. | Increased footprint. Compiles the literal object code of all compression engines directly into the `pg_z. so`. |
| **Dependency Management** | Requires maintaining runtime payload packages via strict `Requires:` definitions in `.spec` or `control`. | Total autonomy (`self-sufficient`). Zero external payload dependencies on the host OS layer during deployment. |

## `gzip` / `deflate` Hardware Acceleration with `zlib-ng`

For deployments that demand maximum throughput for `gzip` and `deflate`
functions, consider using [zlib-ng][1] instead of stock `zlib`.

`zlib-ng` utilizes modern CPU SIMD vectorization (AVX2, AVX-512, or NEON) and
hardware-accelerated CRC32 instructions, unlocking next-generation
performance (up to 4x faster compression and significantly accelerated
decompression).

If your distribution does not supply `zlib-ng`, you must build it from
source. For implementation details, please review the `Dockerfile` in the
root of this repository, which is used by GitHub Actions to validate code on
every commit.

Note that `zlib-ng` can be compiled in `Compat` mode. A compatibility library
can be placed into your system library path to transparently speed up all
standard `zlib` calls. However, this introduces system-wide risk as it
affects all binaries, including PostgreSQL itself.

A safer approach is to compile `zlib-ng` in `Native` mode and use the native API
wrappers provided in this extension. Alternatively, you can statically link
`zlib-ng` directly into the extension's binary. This ensures that only `pg_z`
uses the optimized `zlib-ng` logic without affecting the rest of the system.

## Custom Extreme Optimization (Zero Runtime Dependencies)

If you strictly require a fully static binary with zero external runtime
dependencies and want to minimize the final footprint of `pg_z.so`, you can
manually compile custom versions of your dependencies from their official
sources.

Ensure that every dependency is explicitly built with the following optimization
compiler flags:

```bash
CFLAGS="-O3 -fPIC -flto"
```

This enables global Link-Time Optimization (LTO) and allows the compiler to
perform aggressive dead code elimination, seamlessly embedding only the
required code segments into the final `pg_z.so` binary.

## Verification: Auditing the Compiled `pg_z.so` Binary

To understand exactly why certain libraries appear in the final binary and
others do not, let us walk through a concrete example. Suppose you run the
following custom configuration command on a Debian system:

```bash
./configure --without-brotli --without-lz4 \
            --with-link-gzip=dynamic \
            --with-link-snappy=dynamic \
            --with-link-zstd=static
```

In this specific scenario, Brotli and LZ4 are completely excluded from the
build. Zstandard is explicitly forced to be embedded inside the extension,
while Gzip (zlib) and Snappy are linked as external dynamic system packages.

You can use standard Linux binary utilities to audit the resulting `pg_z.so`
shared library and verify that the build system respected these exact
constraints. Execute the following verification commands from the repository
root after running `make`.

### 1. Verifying Dynamic Dependencies (`ldd`)

The `ldd` utility prints the complete recursive shared library dependencies
required by the binary at runtime:

```bash
ldd tmp/pg_z.so
```

#### What to look for in the `ldd` output

- **Dynamic Libraries**: You should explicitly see `libz.so` and `libsnappy.so`
  mapped to their respective paths in the system (e.g., `/lib/x86_64-linux-gnu/`).
- **Static Libraries**: Libraries configured as `static` (such as `libzstd.so`,
  `libbrotli.so`, or `liblz4.so`) **must be completely absent** from this list.
- **C++ Runtime**: Notice that `libstdc++.so` and `libgcc_s.so` will automatically
  appear because `libsnappy` is a C++ library and implicitly pulls its standard
  runtime along.

### 2. Inspecting Direct Linker Requirements (`readelf`)

While `ldd` shows a recursive chain, `readelf` allows you to inspect the exact,
direct `NEEDED` entries stamped into the ELF header of your binary by the linker:

```bash
readelf -d tmp/pg_z.so | grep NEEDED
```

#### Ideal Output Example (on Debian with default settings)

```text
 0x0000000000000001 (NEEDED)             Shared library: [libz.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libsnappy.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

If an algorithm module is linked statically or excluded via `--without-*`, its
corresponding shared library file name **must not** appear under any `NEEDED` marker.

### 3. Auditing Embedded Static Symbols (`nm`)

To double-check that the code from static archives (like `libzstd.a` or
`libbrotlienc.a`) was successfully embedded directly into the text section of the
shared object, you can list the defined internal symbols:

```bash
nm -D --defined-only tmp/pg_z.so | grep -E -i 'brotli|gzip|deflate|lz4|snappy|zstd'
```

#### What to look for in the `nm` output

- If an algorithm was configured as `static`, this command will output a long list
  of embedded internal function symbols (e.g., `ZSTD_compressCCtx`). This is the
  definitive proof that the static machine code resides inside your `pg_z.so`.
- If an algorithm was excluded from the build entirely, its symbols will return
  absolutely zero output, ensuring a clean and minimal binary footprint in the
  PostgreSQL process memory.

### 4. Diagnostic and Runtime Verification

The `pg_z_version()` function returns a descriptive text string indicating the
extension version and the exact list of compression algorithms successfully
compiled into the active runtime binary.

This function is fully `IMMUTABLE`, `STRICT`, and `PARALLEL SAFE`, allowing the
PostgreSQL query planner to safely evaluate it within any parallel worker paths.

#### Example Usage of the `pg_z_version`

```sql
postgres=# SELECT pg_z_version();
                 pg_z_version
-----------------------------------------------
 pg_z v1.0 (compiled with: gzip, snappy, zstd)
(1 row)
```

This output dynamically adjusts based on your build configuration, providing a
reliable method for database administrators or migration scripts to verify
available compression capabilities on the fly.

## Kubernetes and CloudNativePG (`cnpg`) Deployment

If you want to install this extension as an "Extension Image" for the
[CloudNativePG][2] (`cnpg`) operator for Kubernetes, please refer to the provided
`Dockerfile` and `cluster.yaml` templates in this project repository.

The `Dockerfile` builds a secure image containing all the necessary assets to
deploy the `pg_z` extension along with required system runtime libraries. This
built image can then be seamlessly attached to a vanilla PostgreSQL v18+
instance running under the `cnpg` operator control.

[1]: https://github.com/zlib-ng/zlib-ng
[2]: https://github.com/cloudnative-pg

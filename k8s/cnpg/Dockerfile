# ==========================================
# Stage 1: Build the extension binaries
# ==========================================
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    autoconf automake \
    zlib1g-dev liblz4-dev libzstd-dev libbrotli-dev \
    wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install PostgreSQL 18 development files from PGDG repository
RUN apt-get update && apt-get install -y curl gnupg2 lsb-release && \
    sh -c 'echo "deb http://postgresql.org $(lsb_release -cs)-pgdg main" > /etc/apt/sources.list.d/pgdg.list' && \
    curl -fsSL https://postgresql.org | apt-key add - && \
    apt-get update && apt-get install -y postgresql-server-dev-18

COPY . /build
WORKDIR /build

# Build the extension using our dynamic configure setup
RUN autoreconf -if && ./configure && make

# ==========================================
# Stage 2: Create the immutable OCI Extension Image
# ==========================================
FROM scratch

# Structure strictly separated for the pg_z extension
# Copy extension manifests and the compiled .so
COPY --from=builder /build/pg_z.control  /extensions/pg_z/
COPY --from=builder /build/pg_z--1.0.sql /extensions/pg_z/
COPY --from=builder /build/tmp/pg_z.so   /extensions/pg_z/

# Copy the EXACT dynamic library .so dependencies discovered by the builder
# This isolates zlib, lz4, and zstd from the base OS inside this image volume
COPY --from=builder /usr/lib/x86_64-linux-gnu/libz.so*       /extensions/pg_z/system/
COPY --from=builder /usr/lib/x86_64-linux-gnu/liblz4.so*     /extensions/pg_z/system/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libzstd.so*    /extensions/pg_z/system/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libbrotli*.so* /extensions/pg_z/system/


# ==============================================================================
# STAGE 1
# ==============================================================================
FROM postgres:18 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ca-certificates \
    curl \
    pkg-config \
    postgresql-server-dev-18 \
    libbrotli-dev \
    liblz4-dev \
    libsnappy-dev \
    libzstd-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN chown -R postgres:postgres /build
USER postgres

# Default configuration builds an "all-in" version applicable for tests.
# Users can override this step locally to pass specific configure flags.
RUN autoreconf -if && ./configure && make clean && make

USER root
RUN make install
USER postgres

# ==============================================================================
# STAGE 2
# ==============================================================================
FROM postgres:18 AS final

RUN apt-get update && apt-get install -y --no-install-recommends \
    libbrotli1 \
    liblz4-1 \
    libsnappy1v5 \
    libzstd1 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

# Copy the compiled extension binary
COPY --from=builder /build/tmp/pg_z.so /usr/lib/postgresql/18/lib/
# Copy LLVM Bitcode files if PostgreSQL 18 JIT compilation is utilized
COPY --from=builder /build/tmp/pg_z*.bc /usr/lib/postgresql/18/lib/bitcode/pg_z/

COPY --from=builder /build/pg_z.control /usr/share/postgresql/18/extension/
COPY --from=builder /build/pg_z--*.sql /usr/share/postgresql/18/extension/

USER postgres
ENV PATH="/usr/lib/postgresql/18/bin:$PATH"

CMD ["postgres"]

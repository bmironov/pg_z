# ==============================================================================
# STAGE 1
# ==============================================================================
FROM postgres:18 AS builder

ARG ZLIB_NG_VERSION=2.3.3

RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ca-certificates \
    curl \
    wget \
    git \
    cmake \
    pkg-config \
    postgresql-server-dev-18 \
    libbrotli-dev \
    liblz4-dev \
    libsnappy-dev \
    libzstd-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Building zlib-ng in Native mode
WORKDIR /build
RUN wget https://github.com/zlib-ng/zlib-ng/archive/refs/tags/${ZLIB_NG_VERSION}.tar.gz \
    && tar -xzf ${ZLIB_NG_VERSION}.tar.gz \
    && mv zlib-ng-${ZLIB_NG_VERSION} zlib-ng \
    && rm ${ZLIB_NG_VERSION}.tar.gz \
    && mkdir -p /build/zlib-ng/build \
    && cd /build/zlib-ng/build \
    && cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX=/opt/zlib-ng \
    && cmake --build . --parallel $(nproc) \
    && cmake --install .

ENV C_INCLUDE_PATH=/opt/zlib-ng/include
ENV CPATH=/opt/zlib-ng/include
ENV LIBRARY_PATH=/opt/zlib-ng/lib64:/opt/zlib-ng/lib
ENV LD_LIBRARY_PATH=$LIBRARY_PATH

# Building pg_z extension
WORKDIR /build/pg_z
RUN chown -R postgres:postgres /build/pg_z
COPY --chown=postgres:postgres . .
USER postgres

RUN git config --global --add safe.directory /build/pg_z

# Default configuration builds an "all-in" version
# Users can override this step locally to pass specific configure flags
# See documentation for details
RUN autoreconf -if && ./configure && make clean && make

USER root
RUN make install
USER postgres

# ==============================================================================
# STAGE 2
# ==============================================================================
FROM postgres:18 AS final

ENV PG_LIB=/usr/lib/postgresql/18/lib
ENV PG_EXT=/usr/share/postgresql/18/extension/

RUN apt-get update && apt-get install -y --no-install-recommends \
    libbrotli1 \
    liblz4-1 \
    libsnappy1v5 \
    libzstd1 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

# Copy zlib-ng library
COPY --from=builder /opt/zlib-ng/lib*/libz-ng.* /usr/local/lib/
RUN ldconfig \
    && mkdir -p ${PG_LIB}/bitcode/pg_z/

# Copy the compiled extension binary
COPY --from=builder /build/pg_z/tmp/pg_z.so $PG_LIB
# Copy LLVM Bitcode files if PostgreSQL 18 JIT compilation is utilized
COPY --from=builder /build/pg_z/tmp/*.bc ${PG_LIB}/bitcode/pg_z/

COPY --from=builder /build/pg_z/pg_z.control $PG_EXT
COPY --from=builder /build/pg_z/pg_z--*.sql $PG_EXT

USER postgres
ENV PATH="/usr/lib/postgresql/18/bin:$PATH"

CMD ["postgres"]

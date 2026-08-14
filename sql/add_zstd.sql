--
-- Algorithm: zstd
--
-- NOTE: realisation of Zstandard calls with ability to use own multi-threading
-- deems these functions as PARALLEL UNSAFE to force PostgreSQL to use them in
-- single worker model, yet Zstd compression can operate in multi-thread mode
-- at the same time.
--

-- zstd
 CREATE OR REPLACE FUNCTION zstd(uncompressed bytea, compression_level int DEFAULT 7, threads int DEFAULT 1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_zstd'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL UNSAFE;

 CREATE OR REPLACE FUNCTION zstd(uncompressed text, compression_level int DEFAULT 7, threads int DEFAULT 1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_zstd'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL UNSAFE;

-- unzstd
 CREATE OR REPLACE FUNCTION unzstd(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_unzstd'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL UNSAFE;

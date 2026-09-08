--
-- Algorithm: zstd
--


-- zstd_lib_details
 CREATE OR REPLACE FUNCTION zstd_lib_details()
     RETURNS TABLE (
         algorithm text,
         version text,
         linking text
     )
     AS 'MODULE_PATHNAME', 'pg_zstd_lib_details'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- NOTE: The updated Zstandard integration isolates all internal worker
-- thread workspaces to standard system malloc, completely removing concurrent
-- execution dependencies on the single-threaded PostgreSQL MemoryContext.
-- Consequently, these functions are now strictly PARALLEL SAFE and can be
-- seamlessly executed within parallel PostgreSQL query plans.
-- For optimal production throughput, setting the internal parameter to
-- threads=2 is highly recommended; this prevents shared L3 cache starvation
-- and maximizes physical execution performance on standard hardware topologies.
--

-- zstd
 CREATE OR REPLACE FUNCTION zstd(uncompressed bytea, compression_level int DEFAULT 7, threads int DEFAULT 1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_zstd'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION zstd(uncompressed text, compression_level int DEFAULT 7, threads int DEFAULT 1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_zstd'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- unzstd
 CREATE OR REPLACE FUNCTION unzstd(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_unzstd'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

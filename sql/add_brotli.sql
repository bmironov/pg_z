--
-- Algorithm: brotli
--

-- brotli
CREATE OR REPLACE FUNCTION brotli(uncompressed bytea, compression_level integer default 3)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_brotli'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION brotli(uncompressed text, compression_level integer default 3)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_brotli'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- unbrotli
 CREATE OR REPLACE FUNCTION unbrotli(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_unbrotli'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

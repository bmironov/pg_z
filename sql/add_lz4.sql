--
-- Algorithm: lz4
--

-- lz4
 CREATE OR REPLACE FUNCTION lz4(uncompressed bytea, compression_level integer default 5)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_lz4'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION lz4(uncompressed text, compression_level integer default 5)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_lz4'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- unlz4
 CREATE OR REPLACE FUNCTION unlz4(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_unlz4'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

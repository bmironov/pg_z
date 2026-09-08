--
-- Algorithm: lz4
--

-- lz4_lib_details
 CREATE OR REPLACE FUNCTION lz4_lib_details()
     RETURNS TABLE (
         algorithm text,
         version text,
         linking text
     )
     AS 'MODULE_PATHNAME', 'pg_lz4_lib_details'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

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

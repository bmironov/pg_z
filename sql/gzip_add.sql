--
-- Algorithm: gzip
--

-- deflate
 CREATE OR REPLACE FUNCTION deflate(uncompressed bytea, compression_level integer default -1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_deflate'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION deflate(uncompressed text, compression_level integer default -1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_deflate'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- gzip
 CREATE OR REPLACE FUNCTION gzip(uncompressed bytea, compression_level integer default -1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_gzip'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION gzip(uncompressed text, compression_level integer default -1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_gzip'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- inflate
 CREATE OR REPLACE FUNCTION inflate(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_inflate'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- gunzip
 CREATE OR REPLACE FUNCTION gunzip(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_gunzip'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION ungzip(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_gunzip'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

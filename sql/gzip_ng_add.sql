--
-- Algorithm: gzip_ng
--

-- deflate_ng
 CREATE OR REPLACE FUNCTION deflate_ng(uncompressed bytea, compression_level integer default -1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_deflate_ng'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION deflate_ng(uncompressed text, compression_level integer default -1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_deflate_ng'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- gzip_ng
 CREATE OR REPLACE FUNCTION gzip_ng(uncompressed bytea, compression_level integer default -1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_gzip_ng'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION gzip_ng(uncompressed text, compression_level integer default -1)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_gzip_ng'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- inflate_ng
 CREATE OR REPLACE FUNCTION inflate_ng(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_inflate_ng'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- gunzip_ng
 CREATE OR REPLACE FUNCTION gunzip_ng(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_gunzip_ng'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION ungzip_ng(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_gunzip_ng'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

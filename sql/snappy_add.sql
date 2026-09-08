--
-- Algorithm: snappy
--

-- snappy_lib_details
 CREATE OR REPLACE FUNCTION snappy_lib_details()
     RETURNS TABLE (
         algorithm text,
         version text,
         linking text
     )
     AS 'MODULE_PATHNAME', 'pg_snappy_lib_details'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- snappy
 CREATE OR REPLACE FUNCTION snappy(uncompressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_snappy'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

 CREATE OR REPLACE FUNCTION snappy(uncompressed text)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_snappy'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

-- unsnappy
 CREATE OR REPLACE FUNCTION unsnappy(compressed bytea)
     RETURNS bytea
     AS 'MODULE_PATHNAME', 'pg_unsnappy'
     LANGUAGE 'c'
     IMMUTABLE STRICT
     PARALLEL SAFE;

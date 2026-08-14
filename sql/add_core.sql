
--
-- pg_z_vresion()
--

CREATE OR REPLACE FUNCTION pg_z_version()
    RETURNS text
    AS 'MODULE_PATHNAME', 'pg_z_version'
    LANGUAGE 'c'
    IMMUTABLE STRICT
    PARALLEL SAFE;


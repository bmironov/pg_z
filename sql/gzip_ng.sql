CREATE EXTENSION IF NOT EXISTS pg_z;

-- gzip_ng function tests
SELECT gzip_ng(NULL) AS gzip_ng_null;
SELECT gzip_ng('') AS gzip_ng_blank;
SELECT gzip_ng('\x00'::bytea) AS gzip_ng_zero;

SELECT gzip_ng('The quick brown fox jumps over the lazy dog') AS gzip_ng_default;
SELECT gzip_ng('The quick brown fox jumps over the lazy dog'::bytea) AS gzip_ng_default;
SELECT gzip_ng('The quick brown fox jumps over the lazy dog'::text) AS gzip_ng_default;
SELECT gzip_ng('The quick brown fox jumps over the lazy dog'::bytea, 8) AS gzip_ng_8;

WITH str AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', 10000) AS str
)
SELECT convert_from(gunzip_ng(gzip_ng(str)), 'utf8') = str AS gunzip_ng_long FROM str;

WITH strs AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', generate_series(1, 1000)) AS str
)
SELECT sum((str = convert_from(gunzip_ng(gzip_ng(str)), 'utf8'))::integer) AS gunzip_ng_strings
FROM strs;


-- incorrect compression_level (out of range -1..9) should cause error
SELECT gzip_ng('The quick brown fox jumps over the lazy dog'::bytea, -2) AS gzip_ng_err_1;
SELECT gzip_ng('The quick brown fox jumps over the lazy dog'::bytea, 10) AS gzip_ng_err_2;


-- gunzip_ng function tests
SELECT convert_from(gunzip_ng(gzip_ng('The quick brown fox jumps over the lazy dog')), 'utf8') AS gunzip_ng_ok;
SELECT convert_from(ungzip_ng(gzip_ng('The quick brown fox jumps over the lazy dog')), 'utf8') AS ungzip_ng_ok;
SELECT gunzip_ng(''::bytea) AS gunzip_ng_blank;
SELECT gunzip_ng('\x00'::bytea) AS gunzip_ng_8;
SELECT gunzip_ng('\x0000'::bytea) AS gunzip_ng_16;
SELECT gunzip_ng('fubar'::bytea) AS gunzip_ng_16;
SELECT gunzip_ng(gzip_ng('\x00000000000000000000'::bytea)) AS gzip_ng_roundtrip_zero;

-- error propagation
SELECT gunzip_ng(gzip_ng('The quick brown fox jumps over the lazy dog'::bytea, -2)) AS gunzip_ng_err_1;
SELECT gunzip_ng(gzip_ng('The quick brown fox jumps over the lazy dog'::bytea, 10)) AS gunzip_ng_err_2;


-- check limit set by DB parameter
SHOW pg_z.max_size;
SET pg_z.max_size = 10;
SELECT gzip_ng('The quick brown fox jumps over the lazy dog') AS gzip_ng_overlimit;
SELECT convert_from(gunzip_ng('\x1f8b08000000000000030bc94855282ccd4cce56482aca2fcf5348cbaf50c82acd2d2856c82f4b2d5228014ae72456552aa4e4a7030039a34f412b000000'::bytea), 'UTF8') AS gunzip_ng_overlimit;
RESET pg_z.max_size;


DROP EXTENSION pg_z;

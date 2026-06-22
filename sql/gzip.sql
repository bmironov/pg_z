CREATE EXTENSION IF NOT EXISTS pg_z;

-- gzip function tests
SELECT gzip(NULL) AS gzip_null;
SELECT gzip('') AS gzip_blank;
SELECT gzip('\x00'::bytea) AS gzip_zero;

SELECT gzip('The quick brown fox jumps over the lazy dog') AS gzip_default;
SELECT gzip('The quick brown fox jumps over the lazy dog'::bytea) AS gzip_default;
SELECT gzip('The quick brown fox jumps over the lazy dog'::text) AS gzip_default;
SELECT gzip('The quick brown fox jumps over the lazy dog'::bytea, 8) AS gzip_8;

WITH str AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', 10000) AS str
)
SELECT convert_from(gunzip(gzip(str)), 'utf8') = str AS gunzip_long FROM str;

WITH strs AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', generate_series(1, 1000)) AS str
)
SELECT sum((str = convert_from(gunzip(gzip(str)), 'utf8'))::integer) AS gunzip_strings
FROM strs;


-- incorrect compression_level (out of range -1..9) should cause error
SELECT gzip('The quick brown fox jumps over the lazy dog'::bytea, -2) AS gzip_err_1;
SELECT gzip('The quick brown fox jumps over the lazy dog'::bytea, 10) AS gzip_err_2;


-- gunzip function tests
SELECT convert_from(gunzip(gzip('The quick brown fox jumps over the lazy dog')), 'utf8') AS gunzip_ok;
SELECT gunzip(''::bytea) AS gunzip_blank;
SELECT gunzip('\x00'::bytea) AS gunzip_8;
SELECT gunzip('\x0000'::bytea) AS gunzip_16;
SELECT gunzip('fubar'::bytea) AS gunzip_16;
SELECT gunzip(gzip('\x00000000000000000000'::bytea)) AS gzip_roundtrip_zero;

-- error propagation
SELECT gunzip(gzip('The quick brown fox jumps over the lazy dog'::bytea, -2)) AS gunzip_err_1;
SELECT gunzip(gzip('The quick brown fox jumps over the lazy dog'::bytea, 10)) AS gunzip_err_2;


-- check limit set by DB parameter
SHOW pg_z.max_size;
SET pg_z.max_size = 1000;
SELECT gunzip(gzip(repeat('?', 1100)::bytea)) AS gunzip_overlimit;
RESET pg_z.max_size;


DROP EXTENSION pg_z;

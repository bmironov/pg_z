CREATE EXTENSION IF NOT EXISTS pg_z;

-- zstd function tests
SELECT zstd(NULL) AS zstd_null;
SELECT zstd('') AS zstd_blank;
SELECT zstd('\x00'::bytea) AS zstd_zero;

SELECT zstd('The quick brown fox jumps over the lazy dog') AS zstd_default;
SELECT zstd('The quick brown fox jumps over the lazy dog'::bytea) AS zstd_default;
SELECT zstd('The quick brown fox jumps over the lazy dog'::text) AS zstd_default;
SELECT zstd('The quick brown fox jumps over the lazy dog'::bytea, 8) AS zstd_8_1;
SELECT zstd('The quick brown fox jumps over the lazy dog'::bytea, 8, 4) AS zstd_8_4;

WITH str AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', 10000) AS str
)
SELECT convert_from(unzstd(zstd(str)), 'utf8') = str AS unzstd_long FROM str;

WITH strs AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', generate_series(1, 1000)) AS str
)
SELECT sum((str = convert_from(unzstd(zstd(str)), 'utf8'))::integer) AS unzstd_strings
FROM strs;


-- incorrect compression_level (out of range 1..19) shouldn't cause an error
SELECT zstd('The quick brown fox jumps over the lazy dog'::bytea, -100) AS zstd_err_1;
SELECT zstd('The quick brown fox jumps over the lazy dog'::bytea, 100) AS zstd_err_2;


-- unzstd function tests
SELECT convert_from(unzstd(zstd('The quick brown fox jumps over the lazy dog')), 'utf8') AS unzstd_ok;
SELECT unzstd(''::bytea) AS unzstd_blank;
SELECT unzstd('\x00'::bytea) AS unzstd_8;
SELECT unzstd('\x0000'::bytea) AS unzstd_16;
SELECT unzstd('fubar'::bytea) AS unzstd_16;
SELECT unzstd(zstd('\x00000000000000000000'::bytea)) AS zstd_roundtrip_zero;

SELECT unzstd(zstd('The quick brown fox jumps over the lazy dog'::bytea, -100)) AS unzstd_err_1;
SELECT unzstd(zstd('The quick brown fox jumps over the lazy dog'::bytea, 100)) AS unzstd_err_2;


-- check limit set by DB parameter
SHOW pg_z.max_size;
SET pg_z.max_size = 1000;
SELECT unzstd(zstd(repeat('?', 1100)::bytea)) AS unzstd_overlimit;
RESET pg_z.max_size;


DROP EXTENSION pg_z;

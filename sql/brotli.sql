CREATE EXTENSION IF NOT EXISTS pg_z;

-- brotli function tests
SELECT brotli(NULL) AS brotli_null;
SELECT brotli('') AS brotli_blank;
SELECT brotli('\x00'::bytea) AS brotli_zero;

SELECT brotli('The quick brown fox jumps over the lazy dog') AS brotli_default;
SELECT brotli('The quick brown fox jumps over the lazy dog'::bytea) AS brotli_default;
SELECT brotli('The quick brown fox jumps over the lazy dog'::text) AS brotli_default;
SELECT brotli('The quick brown fox jumps over the lazy dog'::bytea, 8) AS brotli_8;

WITH str AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', 10000) AS str
)
SELECT convert_from(unbrotli(brotli(str)), 'utf8') = str AS unbrotli_long FROM str;

WITH strs AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', generate_series(1, 1000)) AS str
)
SELECT sum((str = convert_from(unbrotli(brotli(str)), 'utf8'))::integer) AS unbrotli_strings
FROM strs;


-- incorrect compression_level (out of range 1..11) shouldn't cause an error
SELECT brotli('The quick brown fox jumps over the lazy dog'::bytea, -100) AS brotli_err_1;
SELECT brotli('The quick brown fox jumps over the lazy dog'::bytea, 100) AS brotli_err_2;


-- unbrotli function tests
SELECT convert_from(unbrotli(brotli('The quick brown fox jumps over the lazy dog')), 'utf8') AS unbrotli_ok;
SELECT unbrotli(''::bytea) AS unbrotli_blank;
SELECT unbrotli('\x00'::bytea) AS unbrotli_8;
SELECT unbrotli('\x0000'::bytea) AS unbrotli_16;
SELECT unbrotli('fubar'::bytea) AS unbrotli_16;
SELECT unbrotli(brotli('\x00000000000000000000'::bytea)) AS brotli_roundtrip_zero;

SELECT unbrotli(brotli('The quick brown fox jumps over the lazy dog'::bytea, -100)) AS unbrotli_err_1;
SELECT unbrotli(brotli('The quick brown fox jumps over the lazy dog'::bytea, 100)) AS unbrotli_err_2;


-- check limit set by DB parameter
SHOW pg_z.max_size;
SET pg_z.max_size = 1000;
SELECT unbrotli(brotli(repeat('?', 1100)::bytea)) AS unbrotli_overlimit;
RESET pg_z.max_size;


DROP EXTENSION pg_z;

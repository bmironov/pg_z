CREATE EXTENSION IF NOT EXISTS pg_z;

-- deflate_ng function tests
SELECT deflate_ng(NULL) AS deflate_ng_null;
SELECT deflate_ng('') AS deflate_ng_blank;
SELECT deflate_ng('\x00'::bytea) AS deflate_ng_zero;

SELECT deflate_ng('The quick brown fox jumps over the lazy dog') AS deflate_ng_default;
SELECT deflate_ng('The quick brown fox jumps over the lazy dog'::bytea) AS deflate_ng_default;
SELECT deflate_ng('The quick brown fox jumps over the lazy dog'::text) AS deflate_ng_default;
SELECT deflate_ng('The quick brown fox jumps over the lazy dog'::bytea, 8) AS deflate_ng_8;

WITH str AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', 10000) AS str
)
SELECT convert_from(inflate_ng(deflate_ng(str)), 'utf8') = str AS inflate_ng_long FROM str;

WITH strs AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', generate_series(1, 1000)) AS str
)
SELECT sum((str = convert_from(inflate_ng(deflate_ng(str)), 'utf8'))::integer) AS inflate_ng_strings
FROM strs;


-- incorrect compression_level (out of range -1..9) should cause error
SELECT deflate_ng('The quick brown fox jumps over the lazy dog'::bytea, -2) AS deflate_ng_err_1;
SELECT deflate_ng('The quick brown fox jumps over the lazy dog'::bytea, 10) AS deflate_ng_err_2;


-- inflate_ng function tests
SELECT convert_from(inflate_ng(deflate_ng('The quick brown fox jumps over the lazy dog')), 'utf8') AS inflate_ng_ok;
SELECT inflate_ng(''::bytea) AS inflate_ng_blank;
SELECT inflate_ng('\x00'::bytea) AS inflate_ng_8;
SELECT inflate_ng('\x0000'::bytea) AS inflate_ng_16;
SELECT inflate_ng('fubar'::bytea) AS inflate_ng_16;
SELECT inflate_ng(deflate_ng('\x00000000000000000000'::bytea)) AS deflate_ng_roundtrip_zero;

-- error propagation
SELECT inflate_ng(deflate_ng('The quick brown fox jumps over the lazy dog'::bytea, -2)) AS inflate_ng_err_1;
SELECT inflate_ng(deflate_ng('The quick brown fox jumps over the lazy dog'::bytea, 10)) AS inflate_ng_err_2;


-- check limit set by DB parameter
SHOW pg_z.max_size;
SET pg_z.max_size = 10;
SELECT deflate_ng('The quick brown fox jumps over the lazy dog') AS deflate_ng_overlimit;
SELECT convert_from(inflate_ng('\x0bc94855282ccd4cce56482aca2fcf5348cbaf50c82acd2d2856c82f4b2d5228014ae72456552aa4e4a70300'::bytea), 'UTF8') AS inflate_ng_overlimit;
RESET pg_z.max_size;


DROP EXTENSION pg_z;

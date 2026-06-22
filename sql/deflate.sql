CREATE EXTENSION IF NOT EXISTS pg_z;

-- deflate function tests
SELECT deflate(NULL) AS deflate_null;
SELECT deflate('') AS deflate_blank;
SELECT deflate('\x00'::bytea) AS deflate_zero;

SELECT deflate('The quick brown fox jumps over the lazy dog') AS deflate_default;
SELECT deflate('The quick brown fox jumps over the lazy dog'::bytea) AS deflate_default;
SELECT deflate('The quick brown fox jumps over the lazy dog'::text) AS deflate_default;
SELECT deflate('The quick brown fox jumps over the lazy dog'::bytea, 8) AS deflate_8;

WITH str AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', 10000) AS str
)
SELECT convert_from(inflate(deflate(str)), 'utf8') = str AS inflate_long FROM str;

WITH strs AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', generate_series(1, 1000)) AS str
)
SELECT sum((str = convert_from(inflate(deflate(str)), 'utf8'))::integer) AS inflate_strings
FROM strs;


-- incorrect compression_level (out of range -1..9) should cause error
SELECT deflate('The quick brown fox jumps over the lazy dog'::bytea, -2) AS deflate_err_1;
SELECT deflate('The quick brown fox jumps over the lazy dog'::bytea, 10) AS deflate_err_2;


-- inflate function tests
SELECT convert_from(inflate(deflate('The quick brown fox jumps over the lazy dog')), 'utf8') AS inflate_ok;
SELECT inflate(''::bytea) AS inflate_blank;
SELECT inflate('\x00'::bytea) AS inflate_8;
SELECT inflate('\x0000'::bytea) AS inflate_16;
SELECT inflate('fubar'::bytea) AS inflate_16;
SELECT inflate(deflate('\x00000000000000000000'::bytea)) AS deflate_roundtrip_zero;

-- error propagation
SELECT inflate(deflate('The quick brown fox jumps over the lazy dog'::bytea, -2)) AS inflate_err_1;
SELECT inflate(deflate('The quick brown fox jumps over the lazy dog'::bytea, 10)) AS inflate_err_2;


-- check limit set by DB parameter
SHOW pg_z.max_size;
SET pg_z.max_size = 1000;
SELECT inflate(deflate(repeat('?', 1100)::bytea)) AS deflate_overlimit;
RESET pg_z.max_size;


DROP EXTENSION pg_z;

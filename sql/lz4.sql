CREATE EXTENSION IF NOT EXISTS pg_z;

-- lz4 function tests
SELECT lz4(NULL) AS lz4_null;
SELECT lz4('') AS lz4_blank;
SELECT lz4('\x00'::bytea) AS lz4_zero;

SELECT lz4('The quick brown fox jumps over the lazy dog') AS lz4_default;
SELECT lz4('The quick brown fox jumps over the lazy dog'::bytea) AS lz4_default;
SELECT lz4('The quick brown fox jumps over the lazy dog'::text) AS lz4_default;
SELECT lz4('The quick brown fox jumps over the lazy dog'::bytea, 8) AS lz4_8;

WITH str AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', 10000) AS str
)
SELECT convert_from(unlz4(lz4(str)), 'utf8') = str AS unlz4_long FROM str;

WITH strs AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', generate_series(1, 1000)) AS str
)
SELECT sum((str = convert_from(unlz4(lz4(str)), 'utf8'))::integer) AS unlz4_strings
FROM strs;


-- incorrect compression_level (out of range 0..12) should cause error
SELECT lz4('The quick brown fox jumps over the lazy dog'::bytea, -2) AS lz4_err_1;
SELECT lz4('The quick brown fox jumps over the lazy dog'::bytea, 13) AS lz4_err_2;


-- unlz4 function tests
SELECT convert_from(unlz4(lz4('The quick brown fox jumps over the lazy dog')), 'utf8') AS unlz4_ok;
SELECT unlz4(''::bytea) AS unlz4_blank;
SELECT unlz4('\x00'::bytea) AS unlz4_8;
SELECT unlz4('\x0000'::bytea) AS unlz4_16;
SELECT unlz4('fubar'::bytea) AS unlz4_16;
SELECT unlz4(lz4('\x00000000000000000000'::bytea)) AS lz4_roundtrip_zero;

-- error propagation
SELECT unlz4(lz4('The quick brown fox jumps over the lazy dog'::bytea, -2)) AS unlz4_err_1;
SELECT unlz4(lz4('The quick brown fox jumps over the lazy dog'::bytea, 13)) AS unlz4_err_2;


-- check limit set by DB parameter
SHOW pg_z.max_size;
SET pg_z.max_size = 1000;
SELECT unlz4(lz4(repeat('?', 1100)::bytea)) AS unlz4_overlimit;
RESET pg_z.max_size;


DROP EXTENSION pg_z;

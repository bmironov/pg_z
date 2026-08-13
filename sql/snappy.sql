CREATE EXTENSION IF NOT EXISTS pg_z;

-- snappy function tests
SELECT snappy(NULL) AS snappy_null;
SELECT snappy('') AS snappy_blank;
SELECT snappy('\x00'::bytea) AS snappy_zero;

SELECT snappy('The quick brown fox jumps over the lazy dog') AS snappy_default;
SELECT snappy('The quick brown fox jumps over the lazy dog'::bytea) AS snappy_default;
SELECT snappy('The quick brown fox jumps over the lazy dog'::text) AS snappy_default;

WITH str AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', 10000) AS str
)
SELECT convert_from(unsnappy(snappy(str)), 'utf8') = str AS unsnappy_long FROM str;

WITH strs AS (
    SELECT repeat('The quick brown fox jumps over the lazy dog', generate_series(1, 1000)) AS str
)
SELECT sum((str = convert_from(unsnappy(snappy(str)), 'utf8'))::integer) AS unsnappy_strings
FROM strs;


-- unsnappy function tests
SELECT convert_from(unsnappy(snappy('The quick brown fox jumps over the lazy dog')), 'utf8') AS unsnappy_ok;
SELECT unsnappy(''::bytea) AS unsnappy_blank;
SELECT unsnappy('\x00'::bytea) AS unsnappy_8;
SELECT unsnappy('\x0000'::bytea) AS unsnappy_16;
SELECT unsnappy('fubar'::bytea) AS unsnappy_16;
SELECT unsnappy(snappy('\x00000000000000000000'::bytea)) AS snappy_roundtrip_zero;

-- check limit set by DB parameter
SHOW pg_z.max_size;
SET pg_z.max_size = 10;
SELECT snappy('The quick brown fox jumps over the lazy dog'::bytea) AS snappy_overlimit;
SELECT convert_from(unsnappy('\xff060000734e61507059012f00009c2f8baa54686520717569636b2062726f776e20666f78206a756d7073206f76657220746865206c617a7920646f67'::bytea), 'UTF8') AS unsnappy_overlimit;
RESET pg_z.max_size;


DROP EXTENSION pg_z;

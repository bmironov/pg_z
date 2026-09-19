CREATE EXTENSION IF NOT EXISTS pg_z;

-- zstd_lib_details
SELECT COUNT(*) FROM zstd_lib_details();
SELECT COUNT(*) FROM (
    SELECT unnest(array[algorithm, version, linking]) FROM zstd_lib_details()
);

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


-- incorrect compression_level (out of range 1..22) shouldn't cause an error
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
SET pg_z.max_size = 10;
SELECT zstd('The quick brown fox jumps over the lazy dog') AS zstd_overlimit;
SELECT convert_from(unzstd('\x28b52ffd202b59010054686520717569636b2062726f776e20666f78206a756d7073206f76657220746865206c617a7920646f67'::bytea), 'UTF8') AS unzstd_overlimit;
RESET pg_z.max_size;


-- test compression with dictionary

-- dictionary storage table
CREATE TEMP TABLE test_dictionaries (
    id int PRIMARY KEY,
    dict_name text,
    dict_data bytea
);

-- Populate with two distinct sample dictionaries mimicking common patterns.
-- Note: In production, these should be binary dictionary profiles trained
-- using 'zstd --train'
INSERT INTO test_dictionaries VALUES
(1, 'json_dict',   '{"user_id":,"event_type":"","timestamp":,"payload":{}}'::bytea),
(2, 'log_dict',    'ERROR [form-processor] failed to process request id='::bytea),
(3, 'lorem_ipsum', 'Lorem ipsum'::bytea);

-- target payloads table
CREATE TEMP TABLE test_data (
    id serial PRIMARY KEY,
    dict_id int,
    payload text
);

INSERT INTO test_data (dict_id, payload) VALUES
(1, '{"user_id":1001,"event_type":"click","timestamp":1711234560,"payload":{"button":"green"}}'),
(1, '{"user_id":1002,"event_type":"view","timestamp":1711234565,"payload":{"page":"home"}}'),
(2, 'ERROR [form-processor] failed to process request id=99234, timeout from auth service'),
(2, 'ERROR [form-processor] failed to process request id=99235, database connection dead lock'),
(3, repeat('Lorem ipsum dolor', 5)),
(3, repeat('Lorem ipsum ornare', 5));

-- Test: Data Integrity and Round-trip Verification
SELECT td.id,
    convert_from(unzstd(zstd(td.payload::bytea, dict.dict_data), dict.dict_data), 'UTF8')
        = td.payload AS data_restored_perfectly
FROM test_data td
LEFT JOIN test_dictionaries dict ON td.dict_id = dict.id;


-- Test: Cache Isolation Validation (Multi-Dictionary Statement Query)
SELECT
    td.id,
    dict.dict_name,
    octet_length(zstd(td.payload::bytea, dict.dict_data)) AS compressed_size,
    convert_from(unzstd(zstd(td.payload::bytea, dict.dict_data), dict.dict_data), 'UTF8') AS verified_text
FROM test_data td
JOIN test_dictionaries dict ON td.dict_id = dict.id
ORDER BY td.id;

-- Test: Compression Efficiency Metrics Comparison
SELECT
    td.id,
    octet_length(td.payload::bytea) AS raw_bytes,
    octet_length(zstd(td.payload::bytea)) AS vanilla_compressed_bytes,
    octet_length(zstd(td.payload::bytea, dict.dict_data)) AS dict_compressed_bytes,
    (octet_length(zstd(td.payload::bytea))
        - octet_length(zstd(td.payload::bytea, dict.dict_data))) AS bytes_saved_by_dictionary
FROM test_data td
JOIN test_dictionaries dict ON td.dict_id = dict.id;


DROP EXTENSION pg_z;

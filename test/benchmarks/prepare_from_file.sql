\o /dev/null

-- Show ERRORs only and turn off NOTICEs and WARNINGs
SET client_min_messages TO error;
\set QUIET on

\timing off

DROP EXTENSION IF EXISTS pg_z;
DROP TABLE IF EXISTS temp_benchmark_data;

CREATE EXTENSION pg_z;

-- making sure we have enough memory for our benchmark
SET work_mem = '256MB';

-- TEMP table will hold simulated log with repeating data that is great for
-- compression algorithms.
-- One line of data is ~571 bytes.
-- Overall size ~110MB
CREATE TABLE temp_benchmark_data (
	raw_text TEXT,
	compressed_data BYTEA
);

INSERT INTO temp_benchmark_data (raw_text, compressed_data)
VALUES (
    pg_read_file('/tmp/xml_big.xml'),
    '?'
);

\o
\echo Prepared following test data set:
-- getting exact size of test data
SELECT
    to_char(octet_length(raw_text)::bigint, '999,999,999') AS dataset_size_bytes,
    pg_size_pretty(octet_length(raw_text)::bigint) AS dataset_size_pretty
FROM temp_benchmark_data;

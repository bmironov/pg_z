\ir prepare.sql


\timing on

\echo TEST Snappy: benchmark compression and decompression

-- Test compression speed (level 1 is fastest)
\echo 'Snappy started...'
SELECT octet_length(snappy(raw_text)) AS compressed_snappy_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
UPDATE temp_benchmark_data SET compressed_data = snappy(raw_text);

-- Test decompression speed
\echo 'UNSnappy started...'
SELECT octet_length(unsnappy(compressed_data)) AS decompressed_snappy_bytes
FROM temp_benchmark_data;


\ir finish.sql

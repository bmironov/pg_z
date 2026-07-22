\ir prepare.sql


\timing on

\echo TEST brotli: benchmark compression and decompression

-- Test compression speed
\echo 'Brotli started...'
SELECT octet_length(brotli(raw_text, 6)) AS compressed_brotli_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
UPDATE temp_benchmark_data SET compressed_data = brotli(raw_text, 6);

-- Test decompression speed
\echo 'Unbrotli started...'
SELECT octet_length(brotli(compressed_data)) AS decompressed_brotli_bytes
FROM temp_benchmark_data;


\ir finish.sql

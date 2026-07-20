\ir prepare.sql


\timing on

\echo TEST LZ4: benchmark compression and decompression

-- Test compression speed (level 1 is fastest)
\echo 'LZ4 started...'
SELECT octet_length(lz4(raw_text, 1)) AS compressed_lz4_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
UPDATE temp_benchmark_data SET compressed_data = lz4(raw_text, 6);

-- Test decompression speed
\echo 'UNLZ4 started...'
SELECT octet_length(unlz4(compressed_data)) AS decompressed_lz4_bytes
FROM temp_benchmark_data;


\ir finish.sql

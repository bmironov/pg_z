\ir prepare.sql


\timing on

\echo TEST deflate: benchmark compression and decompression

-- Test compression speed
\echo 'Deflate started...'
SELECT octet_length(deflate(raw_text, 6)) AS compressed_deflate_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
UPDATE temp_benchmark_data SET compressed_data = deflate(raw_text, 6);

-- Test decompression speed
\echo 'Inflate started...'
SELECT octet_length(inflate(compressed_data)) AS decompressed_deflate_bytes
FROM temp_benchmark_data;


\ir finish.sql

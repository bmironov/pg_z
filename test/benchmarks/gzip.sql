\ir prepare.sql


\timing on

\echo TEST Gzip: benchmark compression and decompression

-- Test compression speed
\echo 'GZIP started...'
SELECT octet_length(gzip(raw_text, 6)) AS compressed_gzip_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
UPDATE temp_benchmark_data SET compressed_data = gzip(raw_text, 6);

-- Test decompression speed
\echo 'GUNZIP started...'
SELECT octet_length(gunzip(compressed_data)) AS decompressed_gzip_bytes
FROM temp_benchmark_data;

\echo 'UNGZIP started...'
SELECT octet_length(ungzip(compressed_data)) AS decompressed_gzip_bytes
FROM temp_benchmark_data;


\ir finish.sql

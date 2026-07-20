\ir prepare.sql


\timing on

\echo TEST Zstandard: benchmark compression and decompression

-- Test compression speed (level 1 is fastest)
\echo 'ZSTD-1 started...'
SELECT octet_length(zstd(raw_text, 1)) AS compressed_zstd_1_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
UPDATE temp_benchmark_data SET compressed_data = zstd(raw_text, 1);

-- Test decompression speed
\echo 'UNZSTD-1 started...'
SELECT octet_length(unzstd(compressed_data)) AS decompressed_zstd_1_bytes
FROM temp_benchmark_data;

\echo 'ZSTD-7 started...'
SELECT octet_length(zstd(raw_text, 7)) AS compressed_zstd_7_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
UPDATE temp_benchmark_data SET compressed_data = zstd(raw_text, 7);

-- Test decompression speed
\echo 'UNZSTD-7 started...'
SELECT octet_length(unzstd(compressed_data)) AS decompressed_zstd_7_bytes
FROM temp_benchmark_data;

\echo 'ZSTD-19 started...'
SELECT octet_length(zstd(raw_text, 19)) AS compressed_zstd_19_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
UPDATE temp_benchmark_data SET compressed_data = zstd(raw_text, 19);

-- Test decompression speed
\echo 'UNZSTD-19 started...'
SELECT octet_length(unzstd(compressed_data)) AS decompressed_zstd_19_bytes
FROM temp_benchmark_data;


\ir finish.sql

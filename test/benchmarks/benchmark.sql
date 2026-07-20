\timing off
DROP EXTENSION IF EXISTS pg_z;
DROP TABLE IF EXISTS temp_benchmark_data;
DROP TABLE IF EXISTS temp_compressed_gzip;
DROP TABLE IF EXISTS temp_compressed_lz4;
DROP TABLE IF EXISTS temp_compressed_zstd_1;
DROP TABLE IF EXISTS temp_compressed_zstd_7;
DROP TABLE IF EXISTS temp_compressed_zstd_19;

CREATE EXTENSION pg_z;

-- makking sure we have enough memory for our benchmark
SET work_mem = '256MB';

-- TEMP table will hold simulated log with repeating data that is great for
-- compression algorithms.
-- One line of data is ~571 bytes.
-- Overall size ~110MB
CREATE TEMPORARY TABLE temp_benchmark_data AS
SELECT
    string_agg(
        'ID:' || i || ' | TIMESTAMP: ' || now()
        || ' | MESSAGE: Lorem ipsum dolor sit amet, consectetur adipiscing elit.'
        || ' Aliquam metus nisi, ultricies consequat faucibus ac, semper a risus.'
        || ' Phasellus finibus porta varius. Quisque et ante orci. Sed eget diam'
        || ' felis. Etiam et varius libero. Fusce blandit pharetra tristique.'
        || ' Sed placerat eu ligula ac malesuada. Vivamus mattis faucibus libero'
        || ' id tristique. Quisque nunc libero, sagittis vitae augue sit amet,'
        || ' mollis vulputate augue. Mauris vitae facilisis purus, id cursus '
        || ' turpis. Sed ultrices hendrerit mauris non eleifend. | ',
        ''
    )::text AS raw_text
FROM generate_series(1, 200000) AS i;

-- getting exact size of test data
SELECT
    octet_length(raw_text)::bigint AS data_size_bytes,
    (octet_length(raw_text)::float / 1024 / 1024)::numeric(10,2) AS data_size_mb,
    pg_size_pretty(octet_length(raw_text)::bigint) AS data_size_pretty
FROM temp_benchmark_data;


\timing on
-- ============================================================================
-- Test 1: benchmark ZLIB (GZIP)
-- ============================================================================

-- Test compression speed
\echo 'GZIP started...'
SELECT octet_length(gzip(raw_text, 6)) AS compressed_gzip_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
CREATE TEMPORARY TABLE temp_compressed_gzip AS
SELECT gzip(raw_text, 6) AS compressed_data FROM temp_benchmark_data;

-- Test decompression speed
\echo 'GUNZIP started...'
SELECT octet_length(gunzip(compressed_data)) AS decompressed_gzip_bytes
FROM temp_compressed_gzip;

\echo 'UNGZIP started...'
SELECT octet_length(ungzip(compressed_data)) AS decompressed_gzip_bytes
FROM temp_compressed_gzip;


-- ============================================================================
-- Test 2: benchmark LZ4
-- ============================================================================

-- Test compression speed (level 1 is fastest)
\echo 'LZ4 started...'
SELECT octet_length(lz4(raw_text, 1)) AS compressed_lz4_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
CREATE TEMPORARY TABLE temp_compressed_lz4 AS
SELECT lz4(raw_text, 1) AS compressed_data FROM temp_benchmark_data;

-- Test decompression speed
\echo 'UNLZ4 started...'
SELECT octet_length(unlz4(compressed_data)) AS decompressed_lz4_bytes
FROM temp_compressed_lz4;


-- ============================================================================
-- Test 3: benchmark Zstandard
-- ============================================================================

-- Test compression speed (level 1 is fastest)
\echo 'ZSTD-1 started...'
SELECT octet_length(zstd(raw_text, 1)) AS compressed_zstd_1_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
CREATE TEMPORARY TABLE temp_compressed_zstd_1 AS
SELECT zstd(raw_text, 1) AS compressed_data FROM temp_benchmark_data;

-- Test decompression speed
\echo 'UNZSTD-1 started...'
SELECT octet_length(unzstd(compressed_data)) AS decompressed_zstd_1_bytes
FROM temp_compressed_zstd_1;

\echo 'ZSTD-7 started...'
SELECT octet_length(zstd(raw_text, 7)) AS compressed_zstd_7_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
CREATE TEMPORARY TABLE temp_compressed_zstd_7 AS
SELECT zstd(raw_text, 7) AS compressed_data FROM temp_benchmark_data;

-- Test decompression speed
\echo 'UNZSTD-7 started...'
SELECT octet_length(unzstd(compressed_data)) AS decompressed_zstd_7_bytes
FROM temp_compressed_zstd_7;

\echo 'ZSTD-19 started...'
SELECT octet_length(zstd(raw_text, 19)) AS compressed_zstd_19_bytes
FROM temp_benchmark_data;

-- Saving compressed data for further decompression test
CREATE TEMPORARY TABLE temp_compressed_zstd_19 AS
SELECT zstd(raw_text, 19) AS compressed_data FROM temp_benchmark_data;

-- Test decompression speed
\echo 'UNZSTD-19 started...'
SELECT octet_length(unzstd(compressed_data)) AS decompressed_zstd_19_bytes
FROM temp_compressed_zstd_19;


-- ============================================================================
-- Cleanup
-- ============================================================================
\timing off
\echo 'Cleanup...'
DROP TABLE IF EXISTS temp_benchmark_data;
DROP TABLE IF EXISTS temp_compressed_gzip;
DROP TABLE IF EXISTS temp_compressed_lz4;
DROP TABLE IF EXISTS temp_compressed_zstd_1;
DROP TABLE IF EXISTS temp_compressed_zstd_7;
DROP TABLE IF EXISTS temp_compressed_zstd_19;
RESET work_mem;


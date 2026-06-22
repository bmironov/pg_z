\timing on

-- =====================================================================
-- STEP 1: Reset Environment and Clear Telemetry Buffers
-- =====================================================================
DROP TABLE IF EXISTS zstd_cache_bench;
DROP EXTENSION IF EXISTS pg_z;

CREATE EXTENSION IF NOT EXISTS pg_z;
CREATE EXTENSION IF NOT EXISTS pg_stat_statements;

-- Ensure track_io_timing is enabled to accurately log block execution boundaries
-- NOTE: In a production cluster, changing track_io_timing requires a superuser context
SET track_io_timing = on;

-- Flush telemetry tracking history
SELECT pg_stat_statements_reset();

-- Create a robust 50MB uncompressed dataset
CREATE TABLE zstd_cache_bench AS
SELECT
    1 AS id,
    repeat('Lorem ipsum dolor sit amet, consectetur adipiscing elit.'
        || ' Aliquam metus nisi, ultricies consequat faucibus ac, semper a risus.'
        || ' Phasellus finibus porta varius. Quisque et ante orci. Sed eget diam'
        || ' felis. Etiam et varius libero. Fusce blandit pharetra tristique.'
        || ' Sed placerat eu ligula ac malesuada. Vivamus mattis faucibus libero'
        || ' id tristique. Quisque nunc libero, sagittis vitae augue sit amet,'
        || ' mollis vulputate augue. Mauris vitae facilisis purus, id cursus '
        || ' turpis. Sed ultrices hendrerit mauris non eleifend.'
        || CHR(10), 12000) AS sample_data;

-- Force table statistics optimization so the engine maps execution structures efficiently
VACUUM ANALYZE zstd_cache_bench;

SELECT 'Test data size (bytes): '
       || octet_length(sample_data)
FROM zstd_cache_bench;

-- ===========================================================================
-- STEP 2: Execute Parameterized Performance Iterations
-- NOTE:
-- alias for the query is required to avoid optimization by execution engine
-- that will fold all following queries without it into one
-- ===========================================================================

SELECT /* THREAD_COUNT: 1 */ count(*) AS threads_1 FROM
    (SELECT zstd(sample_data, 3, 1)
        FROM zstd_cache_bench, generate_series(1, 10000)
    )
AS threads_1;


SELECT /* THREAD_COUNT: 2 */ count(*) AS threads_2 FROM
    (SELECT zstd(sample_data, 3, 2)
        FROM zstd_cache_bench, generate_series(1, 10000)
    )
AS threads_2;


SELECT /* THREAD_COUNT: 4 */ count(*) AS threads_4 FROM
    (SELECT zstd(sample_data, 3, 4)
        FROM zstd_cache_bench, generate_series(1, 10000)
    )
AS threads_4;

SELECT /* THREAD_COUNT: 8 */ count(*) AS threads_8 FROM
    (SELECT zstd(sample_data, 3, 8)
        FROM zstd_cache_bench, generate_series(1, 10000)
    )
AS threads_8;

-- =====================================================================
-- STEP 3: Generate Thread Scalability & Cache Metric Report
-- =====================================================================
\echo '\n=== ZSTANDARD THREAD SCALABILITY & BUFFER CACHE TELEMETRY ===\n'

WITH parsed_metrics AS (
    SELECT
        CASE
            WHEN query LIKE '%THREAD_COUNT: 1%' THEN 1
            WHEN query LIKE '%THREAD_COUNT: 2%' THEN 2
            WHEN query LIKE '%THREAD_COUNT: 4%' THEN 4
            WHEN query LIKE '%THREAD_COUNT: 8%' THEN 8
        END AS allocated_threads,
        calls,
        mean_exec_time AS avg_time_ms,
        -- Track blocks fetched directly out of memory
        shared_blks_hit + local_blks_hit AS total_cache_hits,
        -- Track blocks that missed the cache and fell back to filesystem layers
        shared_blks_read + local_blks_read AS total_disk_reads,
        -- Capture pure filesystem read/write timing overheads if tracked
        shared_blk_read_time + shared_blk_write_time AS total_shared_io_time_ms,
        local_blk_read_time + local_blk_write_time AS total_local_io_time_ms
    FROM pg_stat_statements
    WHERE query LIKE '%THREAD_COUNT:%'
      AND query NOT LIKE '%pg_stat_statements%'
),
baseline AS (
    SELECT avg_time_ms FROM parsed_metrics WHERE allocated_threads = 1
)
SELECT
    p.allocated_threads || ' Host Thread(s)' AS configuration,
    round(p.avg_time_ms::numeric, 2) || ' ms' AS avg_latency,
    round((b.avg_time_ms / p.avg_time_ms)::numeric, 2) || 'x' AS scaling_factor,
    p.total_cache_hits AS blocks_hit_in_cache,
    p.total_disk_reads AS blocks_read_from_disk,
    -- Compute the percentage of pages satisfied without going to disk
    CASE
        WHEN (p.total_cache_hits + p.total_disk_reads) = 0 THEN '100.0%'
        ELSE round((p.total_cache_hits::numeric / (p.total_cache_hits + p.total_disk_reads)::numeric * 100.0), 1) || '%'
    END AS cache_hit_ratio,
    round(p.total_shared_io_time_ms::numeric, 2) || ' ms' AS dedicated_shared_io_overhead
FROM parsed_metrics p, baseline b
ORDER BY p.allocated_threads ASC;

-- Cleanup data structures
DROP TABLE zstd_cache_bench;


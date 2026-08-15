\o /dev/null

-- Show ERRORs only and turn off NOTICEs and WARNINGs
SET client_min_messages TO error;
\set QUIET on

-- ============================================================================
-- Cleanup
-- ============================================================================
\timing off

DROP TABLE IF EXISTS temp_benchmark_data;

RESET work_mem;

DROP EXTENSION IF EXISTS pg_z CASCADE;

\o

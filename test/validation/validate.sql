
\timing on

-- =====================================================================
-- Setup test environment
-- =====================================================================
-- Clean up any previous test instances
DROP TABLE IF EXISTS pg_z_tests;
DROP EXTENSION IF EXISTS pg_z;

CREATE EXTENSION pg_z;

-- Create a structured table to hold our payloads and validation metrics
CREATE TABLE pg_z_tests (
    algorithm             VARCHAR(20),
    test_case_name        VARCHAR(100),
    original_text         TEXT,
    compressed_payload    BYTEA,
    decompressed_text     TEXT,
    original_bytes        INT,
    compressed_bytes      INT,
    compression_ratio     NUMERIC(10,2),
    data_integrity_passed BOOLEAN
);

-- =====================================================================
-- Test cases population (Small, Medium, and Large Data Volumes)
-- =====================================================================
DO $$
DECLARE
    v_algo TEXT;
    v_list TEXT[] := ARRAY['deflate', 'gzip', 'lz4', 'zstd'];
BEGIN
    FOREACH v_algo IN ARRAY v_list LOOP
        -- Case 1: Small payload (frequently handles inline raw text, under 2KB)
        INSERT INTO pg_z_tests (algorithm, test_case_name, original_text)
        VALUES (v_algo,
                'Small Payload (Inline Text) [<2kB]',
                'PostgreSQL extension development requires strict memory management. ' ||
                'Always ensure palloc allocations are cleaned up appropriately to prevent leaks!');

        -- Case 2: Medium payload (~20KB, triggers standard out-of-line storage behaviors)
        INSERT INTO pg_z_tests (algorithm, test_case_name, original_text)
        VALUES (v_algo,
                'Medium Payload (Repeated Text Block) [~20kB]',
                repeat('The quick brown fox jumps over the lazy dog. 1234567890. ', 400));

        -- Case 3: Large payload (~1.5MB, simulates highly complex, heavy TOAST chunks)
        INSERT INTO pg_z_tests (algorithm, test_case_name, original_text)
        VALUES (v_algo,
                'Large Payload (Heavy Structural Block) [~1500kB]',
                repeat('Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ' || chr(10), 12000));
    END LOOP;
END $$;

-- =====================================================================
-- Running test for Compression & Decompression
-- =====================================================================
\echo 'Executing pg_Z tests...';


UPDATE pg_z_tests
SET
    compressed_payload = deflate(original_text, 5),
    decompressed_text = convert_from(inflate(deflate(original_text, 5)), 'UTF8')
WHERE algorithm = 'deflate';

UPDATE pg_z_tests
SET
    compressed_payload = gzip(original_text, 5),
    decompressed_text = convert_from(gunzip(gzip(original_text, 5)), 'UTF8')
WHERE algorithm = 'gzip';

UPDATE pg_z_tests
SET
    compressed_payload = lz4(original_text, 5),
    decompressed_text = convert_from(unlz4(lz4(original_text, 5)), 'UTF8')
WHERE algorithm = 'lz4';

-- Compress using level 5 with 4 worker threads
UPDATE pg_z_tests
SET
    compressed_payload = zstd(original_text, 5, 4),
    decompressed_text = convert_from(unzstd(zstd(original_text, 5, 4)), 'UTF8')
WHERE algorithm = 'zstd';

-- =====================================================================
-- Computing metrics and validating bit-by-bit accuracy
-- =====================================================================
UPDATE pg_z_tests
SET
    original_bytes = octet_length(original_text),
    compressed_bytes = octet_length(compressed_payload),
    compression_ratio = round((octet_length(original_text)::numeric / octet_length(compressed_payload)::numeric), 2),
    data_integrity_passed = (original_text = decompressed_text);

-- =====================================================================
-- Generating the final validation report
-- =====================================================================
\echo '\n=== pg_Z extension compression/decompression report ===\n'

SELECT
    algorithm,
    test_case_name,
    original_bytes || ' bytes' AS uncompressed_size,
    compressed_bytes || ' bytes' AS compressed_size,
    compression_ratio || 'x' AS compression_factor,
    CASE
        WHEN data_integrity_passed = TRUE THEN 'PASS'
        ELSE 'FAIL - DATA CORRUPTED'
    END AS validation_status
FROM pg_z_tests
ORDER BY algorithm, test_case_name;

-- Summary Assertion Check
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_z_tests WHERE data_integrity_passed = FALSE) THEN
        RAISE EXCEPTION 'CRITICAL INTEGRITY FAILURE: Decompressed data did not match the original string source.';
    ELSE
        RAISE NOTICE 'SUCCESS: All test blocks successfully recovered with 100%% bitwise fidelity.';
    END IF;
END $$;


\timing off
DROP EXTENSION IF EXISTS pg_z;
DROP TABLE IF EXISTS temp_benchmark_data;

CREATE EXTENSION pg_z;

-- making sure we have enough memory for our benchmark
SET work_mem = '256MB';

-- TEMP table will hold simulated log with repeating data that is great for
-- compression algorithms.
-- One line of data is ~571 bytes.
-- Overall size ~110MB
CREATE TABLE temp_benchmark_data AS
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
    )::text AS raw_text,
    '?'::bytea AS compressed_data
FROM generate_series(1, 200000) AS i;

-- getting exact size of test data
SELECT
    octet_length(raw_text)::bigint AS data_size_bytes,
    (octet_length(raw_text)::float / 1024 / 1024)::numeric(10,2) AS data_size_mb,
    pg_size_pretty(octet_length(raw_text)::bigint) AS data_size_pretty
FROM temp_benchmark_data;

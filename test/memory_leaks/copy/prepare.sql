DROP TABLE IF EXISTS test_table;


CREATE TABLE test_table (
    source_data     TEXT,
    compressed_data BYTEA
);

-- Generate multiple rows of data
INSERT INTO test_table (source_data)
    SELECT ' Lorem ipsum dolor sit amet, consectetur adipiscing elit.'
        || ' Aliquam metus nisi, ultricies consequat faucibus ac, semper a risus.'
        || ' Phasellus finibus porta varius. Quisque et ante orci. Sed eget diam'
        || ' felis. Etiam et varius libero. Fusce blandit pharetra tristique.'
        || ' Sed placerat eu ligula ac malesuada. Vivamus mattis faucibus libero'
        || ' id tristique. Quisque nunc libero, sagittis vitae augue sit amet,'
        || ' mollis vulputate augue. Mauris vitae facilisis purus, id cursus '
        || ' turpis. Sed ultrices hendrerit mauris non eleifend.'
    FROM generate_series(1, 100);

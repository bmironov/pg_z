\timing on

\echo TEST (Snappy): memory leak in exception...

\ir prepare.sql

UPDATE test_table SET compressed_data = snappy(source_data);


SET pg_z.max_size = 1000;


-- Exception in compression
SELECT octet_length(snappy(source_data)) FROM test_table;

-- Exception in decompression
SELECT octet_length(unsnappy(compressed_data)) FROM test_table;


RESET pg_z.max_size;


\ir finish.sql

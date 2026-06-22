\timing on

\echo TEST (LZ4): memory leak in exception...

\ir prepare.sql

UPDATE test_table SET compressed_data = lz4(source_data);


SET pg_z.max_size = 1000;


-- Exception in compression
SELECT octet_length(lz4(source_data)) FROM test_table;

-- Exception in decompression
SELECT octet_length(unlz4(compressed_data)) FROM test_table;


RESET pg_z.max_size;


\ir finish.sql

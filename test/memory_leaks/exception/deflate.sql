\timing on

\echo TEST (Deflate): memory leak in exception...

\ir prepare.sql

UPDATE test_table SET compressed_data = deflate(source_data);


SET pg_z.max_size = 1000;


-- Exception in compression
SELECT octet_length(deflate(source_data)) FROM test_table;

-- Exception in decompression
SELECT octet_length(inflate(compressed_data)) FROM test_table;


RESET pg_z.max_size;


\ir finish.sql

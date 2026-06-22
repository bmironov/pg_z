\timing on

\echo TEST (Gzip): memory leak in exception...

\ir prepare.sql

UPDATE test_table SET compressed_data = gzip(source_data);


SET pg_z.max_size = 1000;


-- Exception in compression
SELECT octet_length(gzip(source_data)) FROM test_table;

-- Exception in decompression
SELECT octet_length(gunzip(compressed_data)) FROM test_table;


RESET pg_z.max_size;


\ir finish.sql

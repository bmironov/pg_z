\timing on

\echo TEST (ZSTD): memory leak in exception...

\ir prepare.sql

UPDATE test_table SET compressed_data = zstd(source_data);


SET pg_z.max_size = 1000;


-- Exception in compression
SELECT octet_length(zstd(source_data)) FROM test_table;

-- Exception in decompression
SELECT octet_length(unzstd(compressed_data)) FROM test_table;


RESET pg_z.max_size;


\ir finish.sql

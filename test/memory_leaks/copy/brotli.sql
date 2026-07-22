\timing on

\echo TEST (Brotli): memory leak during COPY command...

\ir prepare.sql

UPDATE test_table SET compressed_data = brotli(source_data);

--
-- Test for memory leaks in multi-row COPY command
--

COPY ( SELECT encode(brotli(source_data), 'hex') FROM test_table )
TO '/tmp/copy_brotli_test_compressed.dat';

COPY ( SELECT convert_from(unbrotli(compressed_data), 'UTF8') FROM test_table )
TO '/tmp/copy_brotli_test_decompressed.dat';


\ir finish.sql

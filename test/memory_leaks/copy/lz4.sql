\timing on

\echo TEST (LZ4): memory leak during COPY command...

\ir prepare.sql

UPDATE test_table SET compressed_data = lz4(source_data);

--
-- Test for memory leaks in multi-row COPY command
--

COPY ( SELECT encode(lz4(source_data), 'hex') FROM test_table )
TO '/tmp/copy_lz4_test_compressed.dat';

COPY ( SELECT convert_from(unlz4(compressed_data), 'UTF8') FROM test_table )
TO '/tmp/copy_lz4_test_decompressed.dat';


\ir finish.sql

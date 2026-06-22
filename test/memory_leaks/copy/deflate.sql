\timing on

\echo TEST (Deflate): memory leak during COPY command...

\ir prepare.sql

UPDATE test_table SET compressed_data = deflate(source_data);

--
-- Test for memory leaks in multi-row COPY command
--

COPY ( SELECT encode(deflate(source_data), 'hex') FROM test_table )
TO '/tmp/copy_deflate_test_compressed.dat';

COPY ( SELECT convert_from(inflate(compressed_data), 'UTF8') FROM test_table )
TO '/tmp/copy_deflate_test_decompressed.dat';


\ir finish.sql

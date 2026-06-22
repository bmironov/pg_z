\timing on

\echo TEST (Gzip): memory leak during COPY command...

\ir prepare.sql

UPDATE test_table SET compressed_data = gzip(source_data);

--
-- Test for memory leaks in multi-row COPY command
--

COPY ( SELECT encode(gzip(source_data), 'hex') FROM test_table )
TO '/tmp/copy_gzip_test_compressed.dat';

COPY ( SELECT convert_from(gunzip(compressed_data), 'UTF8') FROM test_table )
TO '/tmp/copy_gzip_test_decompressed.dat';


\ir finish.sql

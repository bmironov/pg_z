\timing on

\echo TEST (ZSTD): memory leak during COPY command...

\ir prepare.sql

UPDATE test_table SET compressed_data = zstd(source_data);

--
-- Test for memory leaks in multi-row COPY command
--

COPY ( SELECT encode(zstd(source_data), 'hex') FROM test_table )
TO '/tmp/copy_zstd_test_compressed.dat';

COPY ( SELECT convert_from(unzstd(compressed_data), 'UTF8') FROM test_table )
TO '/tmp/copy_zstd_test_decompressed.dat';


\ir finish.sql

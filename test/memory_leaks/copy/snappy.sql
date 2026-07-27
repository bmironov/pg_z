\timing on

\echo TEST (Snappy): memory leak during COPY command...

\ir prepare.sql

UPDATE test_table SET compressed_data = snappy(source_data);

--
-- Test for memory leaks in multi-row COPY command
--

COPY ( SELECT encode(snappy(source_data), 'hex') FROM test_table )
TO '/tmp/copy_snappy_test_compressed.dat';

COPY ( SELECT convert_from(unsnappy(compressed_data), 'UTF8') FROM test_table )
TO '/tmp/copy_snappy_test_decompressed.dat';


\ir finish.sql

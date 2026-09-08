
CREATE EXTENSION pg_z;

--
-- pg_z_vresion
--
SELECT pg_z_version();

--
-- pg_z_vresion_num
--
SELECT to_hex(pg_z_version_num());

--
-- pg_z_details
--
\set algos $${"Brotli", "Gzip/Deflate", "Gzip-NG/Deflate-NG", "LZ4", "Snappy", "Zstd"}$$
\set links $${"static", "dynamic"}$$

SELECT COUNT(*) FROM pg_z_details();

SELECT COUNT(*) FROM (
    SELECT unnest(array[algorithm, version, linking])
    FROM pg_z_details()
    WHERE algorithm='LZ4'
);

SELECT COUNT(*) FROM pg_z_details() WHERE algorithm='Unknown';

SELECT COUNT(*) FROM pg_z_details() WHERE algorithm = ANY(:algos::TEXT[]);

SELECT COUNT(*) FROM pg_z_details() WHERE algorithm = ANY(:algos::TEXT[]) AND linking = ANY(:links::TEXT[]);

SELECT COUNT(*) FROM pg_z_details() WHERE algorithm != ALL(:algos::TEXT[]);

SELECT COUNT(*) FROM pg_z_details() WHERE linking != ALL(:links::TEXT[]);


DROP EXTENSION pg_z;

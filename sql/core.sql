
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
SELECT COUNT(*) FROM pg_z_details();

SELECT COUNT(*) FROM pg_z_details() WHERE algorithm='Unknown';

SELECT COUNT(*)
FROM pg_z_details()
WHERE algorithm IN ('Brotli', 'GZip', 'Gzip-NG', 'LZ4', 'Snappy', 'Zstd')
GROUP BY algorithm
ORDER BY algorithm;

SELECT COUNT(*)
FROM pg_z_details()
WHERE algorithm IN ('Brotli', 'GZip', 'Gzip-NG', 'LZ4', 'Snappy', 'Zstd')
    AND linking IN ('static', 'dynamic')
GROUP BY algorithm
ORDER BY algorithm;

SELECT COUNT(*)
FROM pg_z_details()
WHERE algorithm IN ('Brotli', 'GZip', 'Gzip-NG', 'LZ4', 'Snappy', 'Zstd')
    AND linking NOT IN ('static', 'dynamic')
GROUP BY algorithm
ORDER BY algorithm;

SELECT COUNT(*) FROM (
    SELECT unnest(array[algorithm, version, linking])
    FROM pg_z_details()
    WHERE algorithm='LZ4'
);

DROP EXTENSION pg_z;

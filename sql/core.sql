
CREATE EXTENSION pg_z;

--
-- pg_z_vresion
--
SELECT pg_z_version();

--
-- pg_z_vresion_num
--
SELECT to_hex(pg_z_version_num());


DROP EXTENSION pg_z;

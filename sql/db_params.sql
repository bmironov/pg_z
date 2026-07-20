
CREATE EXTENSION pg_z;


--
-- pg_z.max_size
--
SHOW pg_z.max_size;
SET pg_z.max_size = 1000;
SHOW pg_z.max_size;
SET pg_z.max_size = -2;
SHOW pg_z.max_size;
SET pg_z.max_size = '10GB';
SHOW pg_z.max_size;

RESET pg_z.max_size;

--
-- pg_z.mem_chunk_size
--
SHOW pg_z.mem_chunk_size;
SET pg_z.mem_chunk_size = '16kB';
SHOW pg_z.mem_chunk_size;
SET pg_z.mem_chunk_size = '5kB';
SHOW pg_z.mem_chunk_size;
SET pg_z.mem_chunk_size = '10GB';
SHOW pg_z.mem_chunk_size;

RESET pg_z.mem_chunk_size;


DROP EXTENSION pg_z;

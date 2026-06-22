\timing on

\echo TEST (Deflate): memory leaks in multiple calls...

DO $$
DECLARE
    i INT;
    txt TEXT := repeat('PostgreSQL Zero-Copy memory manager leak and performance test. ', 5000); -- ~300KB
    res BYTEA;
BEGIN
    FOR i IN 1..100 LOOP
        res := inflate(deflate(txt, 3));
        -- memory should be released immediately after each call
    END LOOP;
END $$;

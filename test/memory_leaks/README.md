# Unit Tests for Memory Leaks in `pg_z` Functions

These tests check for memory leaks in the compression and decompression
functions within:

- PL/pgSQL blocks;
- Simple DML statements;
- Multi-row `COPY` operations.

The purpose of these tests is to run PostgreSQL under `Valgrind` to detect any
unexpected memory leaks inside or around calls to the `pg_z` extension's
functions.

To execute the tests, start PostgreSQL under Valgrind:

```bash
valgrind --show-error-list=yes \
         --tool=memcheck \
         --leak-check=full \
         --leak-resolution=high \
         --show-leak-kinds=all \
         --track-origins=yes \
         --trace-children=yes \
         --trace-syscalls=yes \
         --suppressions=./test/memory_leaks/pg_z.supp \
         --log-file=/tmp/pg_z_leak.log \
         postgres -D /var/lib/postgresql/18/main \
         -c config_file=/etc/postgresql/18/main/postgresql.conf
```

Next, connect to this instance via `psql` and execute the relevant test script.
Once finished, stop Valgrind via `Ctrl-C`) and examine the log file located
in `/tmp`.

## What to Look for?

Significant memory leaks will be visible via simple system checks:

- `top` or `htop` will show growth in the active process's `RES` (resident) memory
footprint.
- When using Huge Memory Pages, monitoring `HugePages_Free` is highly effective.
This kernel value may fluctuate by a few pages during the test, but it should
return to its initial baseline value after the test completes.

You can use the following simple script to monitor the number of available Huge
Memory Pages in real-time:

```bash
watch -n 5 "grep HugePages_Free /proc/meminfo"
```

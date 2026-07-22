# Data Flow and Storage Architecture

<!-- toc -->

- [Top-Level Overview](#top-level-overview)
- [Data Flow Diagram](#data-flow-diagram)
- [Database Schema Implementation](#database-schema-implementation)
    * [1. Create Extension](#1-create-extension)
    * [2. Base Table](#2-base-table)
    * [3. Upstream Compression View](#3-upstream-compression-view)
    * [4. Decompression Trigger Function](#4-decompression-trigger-function)
    * [5. INSTEAD OF Trigger](#5-instead-of-trigger)
    * [6. API Calls Example](#6-api-calls-example)

<!-- tocstop -->

## Top-Level Overview

The `pg_z` extension is designed to process huge documents that arrive from
remote clients to an API server in a compressed form, and are subsequently
saved into the database in their original decompressed form.

This architecture provides several benefits, including:

- **Transactional Outbox Pattern Support:** Allows implementing the
  [Transactional Outbox][1] pattern by using the database as a temporary,
  ACID-compliant storage layer before routing messages to a message broker.
- **Full-Text Search Optimization:** Enables the use of GIN indexing for
  efficient Full-Text search on uncompressed data.
- **Bandwidth Efficiency:** Saves network bandwidth between remote clients and
  the database server at the expense of database server CPU utilization.

This document describes the transparent data flow architecture inside
PostgreSQL for handling compressed documents. The design offloads the
decompression workload from the API server directly to the database engine
using views and `INSTEAD OF` row triggers.

## Data Flow Diagram

This diagram shows an example of a document exchange utilizing
`Content-Encoding: gzip`. The `pg_z` extension also provides support for many
other popular encodings (e.g., `brotli`, `zstd`, `deflate`, `lz4`).

```text
[ WRITE PATH: Save Compressed Document ]

  +--------+              +------------+                +------------+
  | Client |              | API Server |                | PostgreSQL |
  +--------+              +------------+                +------------+
      |                         |                             |
      | 1. HTTP POST            |                             |
      |    Content-Encoding     |                             |
      |    gzip                 |                             |
      +------------------------>|                             |
      |                         | 2. INSERT INTO              |
      |                         |    documents_gzip           |
      |                         +---------------------------->|
      |                         |                             | [ View Trigger ]
      |                         |                             | 3. Invoke:
      |                         |                             |    ungzip()
      |                         |                             |----+
      |                         |                             |    |
      |                         |                             |<---+
      |                         |                             | 4. INSERT INTO
      |                         |                             |    documents
      |                         |                             |----+
      |                         |                             |    |
      |                         |                             |<---+
      |                         |                             |

[ READ PATH: Retrieve Document with gzip Encoding ]

      |                         | 5. SELECT doc_body          |
      |                         |    FROM documents_gzip      |
      |                         +---------------------------->|
      |                         |                             | 6. Return gzip
      |                         |                             |    payload
      |                         |<----------------------------+
      | 7. HTTP 200 OK          |                             |
      |    Content-Encoding     |                             |
      |    gzip                 |                             |
      |<------------------------+                             |
```

## Database Schema Implementation

Below example shows how to prepare database to process `gzip`-encoded
documents. For other encodings you will need to create similar views, functions,
and triggers. Please note that in the center of this design there will be only
one table with documents in their original format regardless of which encoding
was used to deliver them to this database.

### 1. Create Extension

Nothing will work unless you introduce the `pg_z` extension to your database:

```sql
CREATE EXTENSION pg_z;
```

### 2. Base Table

The underlying physical table stores the fully decompressed text layout. Please
note usage of the `COMPRESSION` storage method. It sets compression algorithm
for the TOAST object. You can tune this part further if server uses file system
that allows compression (e.g., `ZFS`).

```sql
CREATE TABLE documents (
    id         BIGINT    GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    doc_body   TEXT      COMPRESSION lz4              NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP    NOT NULL
);
```

### 3. Upstream Compression View

This view serves as the entry point for the API server, exposing the binary
compressed object layer. Please note that a similar view needs to be created
for each supported compression algorithm.

```sql
-- This view returns the gzip representation of the document body
CREATE VIEW documents_gzip AS
SELECT
    id,
    gzip(doc_body::bytea) AS doc_body,
    created_at
FROM documents;
```

### 4. Decompression Trigger Function

The trigger function captures incoming binary data payloads upon execution,
performs zero-copy safe memory stream decompression, and writes the result to
the base table.

```sql
CREATE OR REPLACE FUNCTION documents_gzip_insert()
RETURNS TRIGGER AS $$
BEGIN
    INSERT INTO documents (doc_body)
        VALUES (convert_from(ungzip(NEW.doc_body), 'utf8'))
        RETURNING id INTO NEW.id;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;
```

### 5. INSTEAD OF Trigger

The conditional view trigger transparently intercepts host `INSERT` queries.

```sql
CREATE TRIGGER trigger_documents_gzip_insert
INSTEAD OF INSERT ON documents_gzip
FOR EACH ROW
EXECUTE FUNCTION documents_gzip_insert();
```

### 6. API Calls Example

When the API Server receives a gzip payload, it can execute the statement without
providing an `id`, and instantly get the database-generated sequence value back:

```sql
INSERT INTO documents_gzip (doc_body)
VALUES ('\x1f8b0800000000000003f348cdc9c9d75108cf2fca49510400d0c34aec0d000000'::bytea)
RETURNING id;
```

To send document from DB to client API Server receives it in compressed format:

```sql
SELECT doc_body FROM documents_gzip WHERE id = 42;
```

If API server needs original document it can query main table:

```sql
SELECT doc_body FROM documents WHERE id = 42;
```

[1]: https://microservices.io/patterns/data/transactional-outbox.html

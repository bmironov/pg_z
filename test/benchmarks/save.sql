-- Saving compressed data for further decompression test
UPDATE temp_benchmark_data SET compressed_data = :func(raw_text);

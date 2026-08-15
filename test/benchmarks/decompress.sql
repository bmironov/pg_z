-- Test decompression speed
SELECT octet_length(:func(compressed_data)) FROM temp_benchmark_data;

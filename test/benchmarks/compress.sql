-- Test compression speed
SELECT octet_length(:func(raw_text)) FROM temp_benchmark_data;


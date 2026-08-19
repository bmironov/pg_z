-- Test compression speed
SELECT octet_length(:func(raw_text, :level, :threads)) FROM temp_benchmark_data;


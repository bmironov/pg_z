-- Test compression speed
SELECT octet_length(:func(raw_text, :level)) FROM temp_benchmark_data;


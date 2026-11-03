add_executable(raftpp_benchmark main.cpp submit.cpp disk.cpp uring.cpp device.cpp profiler.cpp profiler_data.cpp)
target_link_libraries(raftpp_benchmark PRIVATE raftpp::raftpp)
target_compile_definitions(raftpp_benchmark PRIVATE
    RAFTPP_BENCHMARK_HAS_IO_URING=$<BOOL:${RAFTPP_HAVE_LINUX_IO_URING_H}>
    RAFTPP_BENCHMARK_HAS_PERF=$<BOOL:${RAFTPP_HAVE_LINUX_PERF_EVENT_H}>)
set_target_properties(raftpp_benchmark PROPERTIES CXX_EXTENSIONS OFF OUTPUT_NAME raftpp-benchmark)

raftpp_use_private_headers(raftpp_benchmark)

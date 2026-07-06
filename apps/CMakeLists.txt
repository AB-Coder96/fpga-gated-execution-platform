if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fgep_benchmark_report.cpp")
    add_executable(fgep_benchmark_report fgep_benchmark_report.cpp)
    target_link_libraries(fgep_benchmark_report PRIVATE fgep)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fgep_spsc_queue_perf.cpp")
    add_executable(fgep_spsc_queue_perf fgep_spsc_queue_perf.cpp)
    target_link_libraries(fgep_spsc_queue_perf PRIVATE fgep)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fgep_spsc_perf_report.cpp")
    add_executable(fgep_spsc_perf_report fgep_spsc_perf_report.cpp)
    target_link_libraries(fgep_spsc_perf_report PRIVATE fgep)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fgep_md_generator.cpp")
    add_executable(fgep_md_generator fgep_md_generator.cpp)
    target_link_libraries(fgep_md_generator PRIVATE fgep)
endif()
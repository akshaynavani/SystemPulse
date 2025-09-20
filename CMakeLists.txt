cmake_minimum_required(VERSION 3.14)
project(SystemPulse VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(SYSTEMPULSE_BUILD_TESTS "Build unit tests" ON)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(systempulse_core
  src/metrics.cpp
  src/proc_parser.cpp
  src/sync.cpp
  src/thread.cpp
  src/collector.cpp
  src/collectors.cpp
  src/csv_writer.cpp
  src/monitor.cpp
  src/process_collector.cpp
  src/worker_pool.cpp
  src/options.cpp
)
target_include_directories(systempulse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(systempulse_core PUBLIC Threads::Threads)

target_compile_definitions(systempulse_core PUBLIC SYSTEMPULSE_VERSION="${PROJECT_VERSION}")

add_executable(systempulse src/main.cpp)
target_link_libraries(systempulse PRIVATE systempulse_core)

add_executable(pulse_worker tools/pulse_worker.cpp)
target_link_libraries(pulse_worker PRIVATE Threads::Threads)

install(TARGETS systempulse pulse_worker RUNTIME DESTINATION bin)

if(SYSTEMPULSE_BUILD_TESTS)
  enable_testing()
  add_executable(systempulse_tests
    tests/test_main.cpp
    tests/test_proc_parser.cpp
    tests/test_queue.cpp
  )
  target_link_libraries(systempulse_tests PRIVATE systempulse_core)
  add_test(NAME systempulse_tests COMMAND systempulse_tests)
endif()

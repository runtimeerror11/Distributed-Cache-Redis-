cmake_minimum_required(VERSION 3.16)
project(distcache VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DISTCACHE_BUILD_TESTS "Build unit tests" ON)
option(DISTCACHE_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
if(DISTCACHE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

# ---- Core library -----------------------------------------------------------
add_library(distcache_core
  src/store/kv_store.cpp
  src/store/lru_policy.cpp
  src/protocol/resp.cpp
  src/cluster/hash_ring.cpp
  src/cluster/membership.cpp
  src/cluster/ownership.cpp
  src/cluster/rebalancer.cpp
  src/replication/replicator.cpp
  src/server/command_handler.cpp
  src/server/tcp_server.cpp
)
target_include_directories(distcache_core PUBLIC include)
target_link_libraries(distcache_core PUBLIC Threads::Threads)

# ---- Executables ------------------------------------------------------------
add_executable(cache_node apps/cache_node.cpp)
target_link_libraries(cache_node PRIVATE distcache_core)

add_executable(coordinator apps/coordinator.cpp)
target_link_libraries(coordinator PRIVATE distcache_core)

add_executable(rehash_experiment experiments/rehash_comparison.cpp)
target_link_libraries(rehash_experiment PRIVATE distcache_core)

add_executable(failure_experiment experiments/failure_availability.cpp)
target_link_libraries(failure_experiment PRIVATE distcache_core)

# ---- Tests ------------------------------------------------------------------
if(DISTCACHE_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googletest)
  enable_testing()

  add_executable(unit_tests
    tests/test_kv_store.cpp
    tests/test_lru.cpp
    tests/test_resp.cpp
    tests/test_hash_ring.cpp
  )
  target_link_libraries(unit_tests PRIVATE distcache_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(unit_tests)
endif()

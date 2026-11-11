cmake_minimum_required(VERSION 3.16)
project(distcache VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DISTCACHE_BUILD_TESTS "Build unit tests and the cluster smoke test" ON)
option(DISTCACHE_SANITIZE "Build with AddressSanitizer + UBSan" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
# container_of uses offsetof on structs that hold std::string. This is the
# standard intrusive-container technique and is supported by GCC and Clang.
add_compile_options(-Wno-invalid-offsetof)
if(DISTCACHE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- Core library -----------------------------------------------------------
add_library(distcache_core
  src/common/time.cpp
  src/common/net.cpp
  src/ds/hashtable.cpp
  src/ds/heap.cpp
  src/protocol/protocol.cpp
  src/store/store.cpp
  src/cluster/hash_ring.cpp
  src/cluster/topology.cpp
  src/server/event_server.cpp
  src/server/cache_node.cpp
  src/server/coordinator.cpp
  src/client/client.cpp
)
target_include_directories(distcache_core PUBLIC include)

# ---- Executables ------------------------------------------------------------
add_executable(cache_node apps/cache_node.cpp)
target_link_libraries(cache_node PRIVATE distcache_core)

add_executable(coordinator apps/coordinator.cpp)
target_link_libraries(coordinator PRIVATE distcache_core)

add_executable(dcache-cli apps/dcache_cli.cpp)
target_link_libraries(dcache-cli PRIVATE distcache_core)

add_executable(rehash_experiment experiments/rehash_comparison.cpp)
target_link_libraries(rehash_experiment PRIVATE distcache_core)

add_executable(loadgen experiments/loadgen.cpp)
target_link_libraries(loadgen PRIVATE distcache_core)

# ---- Tests ------------------------------------------------------------------
# Dependency-free: a tiny test runner in tests/check.h, so the project builds offline.
if(DISTCACHE_BUILD_TESTS)
  enable_testing()
  add_executable(unit_tests
    tests/test_main.cpp
    tests/test_hashtable.cpp
    tests/test_heap.cpp
    tests/test_list.cpp
    tests/test_protocol.cpp
    tests/test_store.cpp
    tests/test_hash_ring.cpp
    tests/test_topology.cpp
  )
  target_link_libraries(unit_tests PRIVATE distcache_core)
  add_test(NAME unit_tests COMMAND unit_tests)
  add_test(NAME cluster_smoke
           COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/smoke_test.sh ${CMAKE_BINARY_DIR})
  set_tests_properties(cluster_smoke PROPERTIES TIMEOUT 120)
endif()

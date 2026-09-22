cmake_minimum_required(VERSION 3.20)
project(carteret LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# -march=native produces a binary that is not portable and a result that cannot
# be reproduced on another machine, so it stays off for anything published. It
# exists only as a labelled experiment.
option(CARTERET_NATIVE "Build with -march=native (excluded from published results)" OFF)
if(CARTERET_NATIVE)
  add_compile_options(-march=native)
endif()

option(CARTERET_ASAN "AddressSanitizer and UndefinedBehaviorSanitizer build" OFF)
if(CARTERET_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# libFuzzer ships with upstream Clang, not with Apple Clang. The fuzz preset
# therefore requires a toolchain override on macOS; tools/fuzz.sh picks a
# suitable compiler and is the supported way to run a campaign.
option(CARTERET_FUZZ "Build libFuzzer targets" OFF)

option(CARTERET_BENCH "Build the benchmark harness" OFF)

add_library(carteret INTERFACE)
target_include_directories(carteret INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Test-only helpers. Not part of the installed interface.
add_library(carteret_testing INTERFACE)
target_link_libraries(carteret_testing INTERFACE carteret)
target_include_directories(carteret_testing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(census         src/census.cpp)
add_executable(replay        src/replay.cpp)
add_executable(gen_synthetic  tools/gen_synthetic.cpp)
target_link_libraries(census        PRIVATE carteret)
target_link_libraries(replay       PRIVATE carteret)
target_link_libraries(gen_synthetic PRIVATE carteret)

enable_testing()
add_executable(test_wire tests/test_wire.cpp)
target_link_libraries(test_wire PRIVATE carteret_testing)
add_test(NAME wire COMMAND test_wire)

add_executable(test_messages tests/test_messages.cpp)
target_link_libraries(test_messages PRIVATE carteret_testing)
add_test(NAME messages COMMAND test_messages)

add_executable(test_dispatch tests/test_dispatch.cpp)
target_link_libraries(test_dispatch PRIVATE carteret_testing)
add_test(NAME dispatch COMMAND test_dispatch)

add_executable(test_reference_book tests/test_reference_book.cpp)
target_link_libraries(test_reference_book PRIVATE carteret_testing)
add_test(NAME reference_book COMMAND test_reference_book)

add_executable(test_order_index tests/test_order_index.cpp)
target_link_libraries(test_order_index PRIVATE carteret_testing)
add_test(NAME order_index COMMAND test_order_index)

add_executable(test_differential tests/test_differential.cpp)
target_link_libraries(test_differential PRIVATE carteret_testing)
add_test(NAME differential COMMAND test_differential)

if(CARTERET_FUZZ)
  # Sanitizers are not optional here: a fuzzer without them finds only inputs
  # that crash outright and misses the out-of-bounds read this target exists
  # to look for.
  add_executable(fuzz_frame tests/fuzz_frame.cpp)
  target_link_libraries(fuzz_frame PRIVATE carteret)
  target_compile_options(fuzz_frame PRIVATE
    -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
  target_link_options(fuzz_frame PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

# Writes a synthetic session and censuses it back, which catches framing
# regressions without requiring a multi-gigabyte download in CI.
add_test(NAME roundtrip
  COMMAND ${CMAKE_COMMAND}
    -DGEN=$<TARGET_FILE:gen_synthetic>
    -DCENSUS=$<TARGET_FILE:census>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip.cmake)

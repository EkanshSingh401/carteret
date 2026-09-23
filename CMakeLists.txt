cmake_minimum_required(VERSION 3.20)
project(carteret LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The warning set is carried on an interface target rather than applied
# directory-wide, so that vendored dependencies fetched below are not held to
# it. A warning in code this project did not write is noise that hides the
# warnings that matter.
add_library(carteret_warnings INTERFACE)
target_compile_options(carteret_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

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
if(CARTERET_BENCH)
  # hdr_histogram_c records a full distribution in bounded memory at a stated
  # precision. Storing raw samples instead would either cap the sample count --
  # biasing the distribution toward the start of the session -- or hold
  # hundreds of megabytes per run.
  include(FetchContent)
  set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(HDR_HISTOGRAM_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
  set(HDR_LOG_REQUIRED             OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(hdr_histogram
    GIT_REPOSITORY https://github.com/HdrHistogram/HdrHistogram_c.git
    GIT_TAG        0.11.8
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(hdr_histogram)
endif()

# Floating-point contraction is off everywhere. A compiler is permitted to
# fuse a*b+c into a single fused multiply-add with one rounding instead of
# two, and arm64 has an FMA it will use by default while baseline x86-64 does
# not. The same source would then produce different numbers on the
# development Mac and on the benchmark host -- silently, and in exactly the
# arithmetic the sampler and the study depend on. See docs/design.md record
# 038.
add_library(carteret_fp INTERFACE)
target_compile_options(carteret_fp INTERFACE -ffp-contract=off)

add_library(carteret INTERFACE)
target_include_directories(carteret INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(carteret INTERFACE carteret_warnings carteret_fp)

# Test-only helpers. Not part of the installed interface.
add_library(carteret_testing INTERFACE)
target_link_libraries(carteret_testing INTERFACE carteret)
target_include_directories(carteret_testing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(census         src/census.cpp)
add_executable(replay        src/replay.cpp)
add_executable(determinism   src/determinism.cpp)
add_executable(export_micro  src/export_micro.cpp)
add_executable(queue_study   src/queue_study.cpp)
add_executable(export_features src/export_features.cpp)
add_executable(gen_synthetic  tools/gen_synthetic.cpp)
# Checksums the exact draws the study consumes, so the benchmark host can make
# the glibc/x86-64 comparison directly instead of it being inferred on arm64.
add_executable(draw_checksum  tools/draw_checksum.cpp)
target_link_libraries(census        PRIVATE carteret)
target_link_libraries(replay       PRIVATE carteret)
target_link_libraries(determinism  PRIVATE carteret)
target_link_libraries(export_micro PRIVATE carteret)
target_link_libraries(queue_study  PRIVATE carteret)
target_link_libraries(export_features PRIVATE carteret)
target_link_libraries(gen_synthetic PRIVATE carteret)
target_link_libraries(draw_checksum PRIVATE carteret)

if(CARTERET_BENCH)
  add_executable(bench_book bench/bench_book.cpp bench/timer.cpp)
  target_link_libraries(bench_book PRIVATE carteret hdr_histogram_static)
  target_compile_definitions(bench_book PRIVATE CARTERET_HAVE_HDR=1)
  # The vendored headers are not held to this project's warning set.
  target_include_directories(bench_book SYSTEM PRIVATE
    ${hdr_histogram_SOURCE_DIR}/include)
endif()

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

add_executable(test_sha256 tests/test_sha256.cpp)
target_link_libraries(test_sha256 PRIVATE carteret_testing)
add_test(NAME sha256 COMMAND test_sha256)

add_executable(test_moldudp64 tests/test_moldudp64.cpp)
target_link_libraries(test_moldudp64 PRIVATE carteret_testing)
add_test(NAME moldudp64 COMMAND test_moldudp64)

add_executable(test_queue_sim tests/test_queue_sim.cpp)
target_link_libraries(test_queue_sim PRIVATE carteret_testing)
add_test(NAME queue_sim COMMAND test_queue_sim)

# One test per correctness gate, each constructing the defect that gate exists
# to catch. A gate that has only ever been seen to pass is not known to check
# anything -- and the reopening-window cap failed a clean session on its first
# run, which is how that stopped being a hypothetical.
add_executable(test_gate_negatives tests/test_gate_negatives.cpp)
target_link_libraries(test_gate_negatives PRIVATE carteret_testing)
add_test(NAME gate_negatives COMMAND test_gate_negatives)

# The download integrity gate, driven against each corruption the archive has
# produced: a short file, a truncated stream, appended bytes, a wrong digest.
add_test(NAME integrity_negative
  COMMAND ${CMAKE_COMMAND}
    -DVERIFY=${CMAKE_SOURCE_DIR}/tools/verify_archive.sh
    -P ${CMAKE_SOURCE_DIR}/tests/integrity_negative.cmake)

# Pins every random draw. The CI matrix runs this under both compilers, which
# is the point: an implementation-defined distribution would diverge between
# them and this is what makes that visible.
add_executable(test_rng_golden tests/test_rng_golden.cpp)
target_link_libraries(test_rng_golden PRIVATE carteret_testing)
add_test(NAME rng_golden COMMAND test_rng_golden)

# Determinism: regenerate the synthetic session and check both hashes against
# the committed golden values. Regenerating rather than committing the session
# also checks that gen_synthetic is itself deterministic.
add_test(NAME determinism
  COMMAND ${CMAKE_COMMAND}
    -DGEN=$<TARGET_FILE:gen_synthetic>
    -DDET=$<TARGET_FILE:determinism>
    -DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/golden_hashes.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/determinism.cmake)

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

cmake_minimum_required(VERSION 3.20)
project(carteret LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# -march=native is OFF by default and should stay off for any number you
# publish: it makes the binary non-portable and the result irreproducible on
# anyone else's machine. Turn it on only as a labelled experiment.
option(CARTERET_NATIVE "Build with -march=native (NOT for published numbers)" OFF)
if(CARTERET_NATIVE)
  add_compile_options(-march=native)
endif()

option(CARTERET_ASAN "AddressSanitizer + UBSan build" OFF)
if(CARTERET_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(carteret INTERFACE)
target_include_directories(carteret INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(census         src/census.cpp)
add_executable(gen_synthetic  tools/gen_synthetic.cpp)
target_link_libraries(census        PRIVATE carteret)
target_link_libraries(gen_synthetic PRIVATE carteret)

enable_testing()
add_executable(test_wire tests/test_wire.cpp)
target_link_libraries(test_wire PRIVATE carteret)
add_test(NAME wire COMMAND test_wire)

# Round trip: write a synthetic session, census it back. Catches framing
# regressions without needing a 13 GB download in CI.
add_test(NAME roundtrip
  COMMAND ${CMAKE_COMMAND}
    -DGEN=$<TARGET_FILE:gen_synthetic>
    -DCENSUS=$<TARGET_FILE:census>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/roundtrip.cmake)

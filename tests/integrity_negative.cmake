# integrity_negative.cmake -- proves the download integrity gate can fail.
#
# tools/verify_archive.sh is what stands between a truncated transfer and a
# study run on half a session. Running it only on good data establishes
# nothing, so this drives it against each corruption the archive has actually
# produced -- a short file, a truncated gzip stream, appended bytes -- plus a
# wrong digest, and requires the documented exit code from each.
#
# Listed in docs/correctness.md with the rest of the gate tests.

set(WORK "${CMAKE_CURRENT_BINARY_DIR}/integrity_negative")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(PLAIN "${WORK}/session.bin")
# Incompressible-ish content, so truncation cannot accidentally still inflate.
set(BODY "")
foreach(i RANGE 0 200)
  string(APPEND BODY "carteret integrity probe line ${i} 0123456789abcdef\n")
endforeach()
file(WRITE "${PLAIN}" "${BODY}")

find_program(GZIP_EXE gzip REQUIRED)
execute_process(COMMAND "${GZIP_EXE}" -k -f "${PLAIN}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "could not gzip the probe file")
endif()
set(GOOD "${WORK}/session.bin.gz")

file(SIZE "${GOOD}" GOOD_SIZE)

macro(expect_exit LABEL EXPECTED)
  execute_process(
    COMMAND sh "${VERIFY}" ${ARGN}
    RESULT_VARIABLE got
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
  if(NOT got EQUAL ${EXPECTED})
    message(FATAL_ERROR
      "${LABEL}: expected exit ${EXPECTED}, got ${got}\n${out}\n${err}")
  endif()
  message(STATUS "ok  ${LABEL} (exit ${got})")
endmacro()

# Positive control first. If this fails, every negative below is meaningless:
# a script that rejects everything is not a gate either.
expect_exit("a good archive passes" 0 "${GOOD}" "${GOOD_SIZE}")

# 1. Short file -- the shape of a dropped connection.
expect_exit("length mismatch is rejected" 2 "${GOOD}" "999999999")

# 2. Truncated gzip stream. Right prefix, wrong file, and gunzip would happily
#    produce a plausible prefix of a session from it.
math(EXPR HALF "${GOOD_SIZE} / 2")
execute_process(
  COMMAND sh -c "head -c ${HALF} '${GOOD}' > '${WORK}/truncated.gz'"
  RESULT_VARIABLE trc)
if(NOT trc EQUAL 0)
  message(FATAL_ERROR "could not build a truncated archive to test against")
endif()
file(SIZE "${WORK}/truncated.gz" TRUNC_SIZE)
if(NOT TRUNC_SIZE EQUAL ${HALF})
  message(FATAL_ERROR "truncated archive is ${TRUNC_SIZE} bytes, expected ${HALF}")
endif()
expect_exit("a truncated gzip stream is rejected" 3 "${WORK}/truncated.gz" "${TRUNC_SIZE}")

# 3. Appended bytes -- what a bad continued transfer produced on this archive.
#    The file is a valid gzip stream followed by garbage, which is the case
#    gzip reports differently on macOS and GNU.
configure_file("${GOOD}" "${WORK}/appended.gz" COPYONLY)
file(APPEND "${WORK}/appended.gz" "this is not part of the gzip stream")
file(SIZE "${WORK}/appended.gz" APP_SIZE)
expect_exit("appended bytes are rejected" 4 "${WORK}/appended.gz" "${APP_SIZE}")

# 4. Wrong digest on an otherwise perfect file.
expect_exit("a wrong sha256 is rejected" 5 "${GOOD}" "${GOOD_SIZE}"
            "0000000000000000000000000000000000000000000000000000000000000000")

# 5. Missing file.
expect_exit("a missing file is rejected" 1 "${WORK}/not-there.gz")

message(STATUS "integrity gate rejects every corruption tested")

# chunk_negative.cmake -- proves the chunked downloader rejects bad responses.
#
# tools/fetch_chunked.sh appends a chunk to a partial file. Everything depends
# on it refusing to append anything that is not the range it asked for, and
# the failure this guards against has already happened once on this archive:
# `curl --continue-at` retried, the server returned the WHOLE body, curl
# appended it, and the result was 115% of the advertised length with a valid
# gzip stream at the front.
#
# The validation is callable as --check-headers so these run offline, against
# header text rather than a network.

set(WORK "${CMAKE_CURRENT_BINARY_DIR}/chunk_negative")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(TOTAL 4764426091)
set(ETAG "\"29fda95e24b9d41:0\"")

macro(write_hdr NAME STATUS RANGE TAG)
  file(WRITE "${WORK}/${NAME}"
    "HTTP/2 ${STATUS} \n"
    "content-type: application/x-gzip\n"
    "accept-ranges: bytes\n"
    "etag: ${TAG}\n"
    "content-range: ${RANGE}\n")
endmacro()

macro(expect_exit LABEL EXPECTED)
  execute_process(
    COMMAND sh "${SCRIPT}" --check-headers ${ARGN}
    RESULT_VARIABLE got OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT got EQUAL ${EXPECTED})
    message(FATAL_ERROR "${LABEL}: expected exit ${EXPECTED}, got ${got}\n${out}\n${err}")
  endif()
  message(STATUS "ok  ${LABEL} (exit ${got})")
endmacro()

# Positive control. A rule that rejects everything is not a check either.
write_hdr(good.txt 206 "bytes 0-1023/${TOTAL}" "${ETAG}")
expect_exit("a correct 206 chunk is accepted" 0
            "${WORK}/good.txt" 0 1023 ${TOTAL} "${ETAG}")

# 1. A 200 is the whole file, not the range asked for. This is the exact
#    response that produced the 115% file.
write_hdr(whole.txt 200 "" "${ETAG}")
expect_exit("a 200 response to a range request is rejected" 10
            "${WORK}/whole.txt" 0 1023 ${TOTAL} "${ETAG}")

# 2. Content-Range describing a different range than was asked for.
write_hdr(shifted.txt 206 "bytes 0-2047/${TOTAL}" "${ETAG}")
expect_exit("a mismatched Content-Range is rejected" 11
            "${WORK}/shifted.txt" 0 1023 ${TOTAL} "${ETAG}")

# 2b. Right range, wrong total: a different file of the same name.
write_hdr(wrongtotal.txt 206 "bytes 0-1023/999999999" "${ETAG}")
expect_exit("a Content-Range with the wrong total is rejected" 11
            "${WORK}/wrongtotal.txt" 0 1023 ${TOTAL} "${ETAG}")

# 2c. Absent Content-Range.
file(WRITE "${WORK}/norange.txt" "HTTP/2 206 \netag: ${ETAG}\n")
expect_exit("a 206 with no Content-Range is rejected" 11
            "${WORK}/norange.txt" 0 1023 ${TOTAL} "${ETAG}")

# 3. The file changed underneath the download.
write_hdr(changed.txt 206 "bytes 0-1023/${TOTAL}" "\"deadbeef:0\"")
expect_exit("a changed ETag is rejected" 12
            "${WORK}/changed.txt" 0 1023 ${TOTAL} "${ETAG}")

# --- 4. One corrupted byte in an assembled file fails verification ---------
#
# Every chunk can be the range that was asked for and the assembly can still
# be wrong. gzip's trailer carries a CRC-32 over the whole uncompressed
# stream, so a single flipped byte anywhere fails `gzip -t`. This is the check
# that makes the chunked path safe without a published digest.
set(PLAIN "${WORK}/payload.bin")
set(BODY "")
foreach(i RANGE 0 400)
  string(APPEND BODY "carteret chunk integrity probe line ${i} 0123456789abcdef\n")
endforeach()
file(WRITE "${PLAIN}" "${BODY}")
find_program(GZIP_EXE gzip REQUIRED)
execute_process(COMMAND "${GZIP_EXE}" -k -f "${PLAIN}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "could not gzip the probe payload")
endif()
set(GOODGZ "${WORK}/payload.bin.gz")
file(SIZE "${GOODGZ}" GZ_SIZE)

# Control: intact.
execute_process(COMMAND "${GZIP_EXE}" -t "${GOODGZ}" RESULT_VARIABLE rc
                OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "intact archive failed gzip -t: ${e}")
endif()
message(STATUS "ok  an intact assembled file passes gzip -t")

# Flip one byte in the middle of the compressed stream.
math(EXPR MID "${GZ_SIZE} / 2")
execute_process(
  COMMAND sh -c "cp '${GOODGZ}' '${WORK}/corrupt.gz' && \
    printf '\\xFF' | dd of='${WORK}/corrupt.gz' bs=1 seek=${MID} count=1 conv=notrunc 2>/dev/null"
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "could not build a corrupted archive")
endif()
file(SIZE "${WORK}/corrupt.gz" C_SIZE)
if(NOT C_SIZE EQUAL ${GZ_SIZE})
  message(FATAL_ERROR "corruption changed the length; it must not")
endif()

execute_process(COMMAND sh "${VERIFY}" --no-published-digest
                        "${WORK}/corrupt.gz" ${C_SIZE}
                RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(rc EQUAL 0)
  message(FATAL_ERROR
    "a file with one corrupted byte passed verification. The length is "
    "unchanged, so only the gzip trailer's CRC-32 can catch this, and it "
    "did not.\n${o}\n${e}")
endif()
message(STATUS "ok  one corrupted byte fails verification (exit ${rc}, length unchanged)")

message(STATUS "chunked downloader rejects every bad response tested")

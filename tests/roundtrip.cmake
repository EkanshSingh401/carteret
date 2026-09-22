# Round trip: write a synthetic session, census it back, and then damage it and
# require the census to notice.
#
# The positive half catches framing regressions without a multi-gigabyte
# download. The negative half is the one that matters for data integrity: a
# session file carries no checksum and, since NASDAQ writes no zero-length
# terminator, a truncated file is a well-formed prefix of a valid file. The
# only thing that distinguishes it is that its last message is not System
# Event 'C'. If the census stopped checking that, truncation would pass
# silently, so the check is tested rather than trusted.

set(SESSION "${CMAKE_CURRENT_BINARY_DIR}/roundtrip.itch")
execute_process(COMMAND ${GEN} ${SESSION} 200000 7 RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gen_synthetic failed")
endif()

execute_process(COMMAND ${CENSUS} ${SESSION} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "census rejected a session it should accept:\n${out}")
endif()
if(NOT out MATCHES "unknown type      0")
  message(FATAL_ERROR "unknown message type in a self-generated stream")
endif()
if(NOT out MATCHES "length mismatch   0")
  message(FATAL_ERROR "length mismatch in a self-generated stream")
endif()
if(NOT out MATCHES "trailing bytes    0")
  message(FATAL_ERROR "bytes left unread in a self-generated stream")
endif()
if(NOT out MATCHES "complete session  yes")
  message(FATAL_ERROR "census did not recognise System Event 'C' as the end")
endif()

# Truncation. A session that stops before its final System Event is a
# well-formed prefix of a valid file: every message in it parses, the framing
# consumes every byte, and nothing about the bytes says it is incomplete. Only
# the missing 'C' does. Generating it with --no-end rather than cutting bytes
# keeps this test independent of file surgery and of the shell.
set(TRUNCATED "${CMAKE_CURRENT_BINARY_DIR}/roundtrip-truncated.itch")
execute_process(COMMAND ${GEN} ${TRUNCATED} 200000 7 --no-end RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gen_synthetic --no-end failed")
endif()

execute_process(COMMAND ${CENSUS} ${TRUNCATED} OUTPUT_VARIABLE out2 RESULT_VARIABLE rc2)
if(rc2 EQUAL 0)
  message(FATAL_ERROR
    "census ACCEPTED a session with no End of Messages. Truncation would pass "
    "silently:\n${out2}")
endif()
if(NOT out2 MATCHES "complete session  NO")
  message(FATAL_ERROR "census did not report the missing terminator:\n${out2}")
endif()

message(STATUS "roundtrip ok: complete session accepted, truncated session rejected")

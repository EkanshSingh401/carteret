# Regenerates the synthetic session and checks both determinism hashes against
# the committed golden values.
#
# The session is regenerated rather than committed, so this also checks that
# gen_synthetic is itself deterministic: a change in the generator moves the
# hashes and fails here, which is the intended behaviour.
file(READ "${GOLDEN}" _golden_text)
string(REGEX MATCHALL "[^\n]+" _lines "${_golden_text}")
set(_row "")
foreach(_line IN LISTS _lines)
  if(NOT _line MATCHES "^#")
    set(_row "${_line}")
  endif()
endforeach()
if(_row STREQUAL "")
  message(FATAL_ERROR "no golden hash row found in ${GOLDEN}")
endif()

separate_arguments(_fields UNIX_COMMAND "${_row}")
list(GET _fields 0 _messages)
list(GET _fields 1 _event_hash)
list(GET _fields 2 _book_hash)

set(SESSION "${CMAKE_CURRENT_BINARY_DIR}/determinism.itch")
execute_process(COMMAND ${GEN} ${SESSION} 200000 7 RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gen_synthetic failed")
endif()

execute_process(
  COMMAND ${DET} --check ${_event_hash} ${_book_hash} ${SESSION}
  OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
message(STATUS "${out}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "determinism hashes changed:\n${err}")
endif()

if(NOT out MATCHES "messages      ${_messages}")
  message(FATAL_ERROR "message count changed; expected ${_messages}\n${out}")
endif()
message(STATUS "determinism ok")

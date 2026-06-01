if (NOT DEFINED INPUT_LIBRARY OR INPUT_LIBRARY STREQUAL "")
    message(FATAL_ERROR "INPUT_LIBRARY is required")
endif ()

if (NOT EXISTS "${INPUT_LIBRARY}")
    message(FATAL_ERROR "Runtime library does not exist: ${INPUT_LIBRARY}")
endif ()

if (NOT DEFINED TARGET_DIR OR TARGET_DIR STREQUAL "")
    message(FATAL_ERROR "TARGET_DIR is required")
endif ()

if (NOT DEFINED OBJDUMP_EXECUTABLE OR OBJDUMP_EXECUTABLE STREQUAL "")
    set(OBJDUMP_EXECUTABLE objdump)
endif ()

execute_process(
        COMMAND "${OBJDUMP_EXECUTABLE}" -p "${INPUT_LIBRARY}"
        RESULT_VARIABLE OBJDUMP_RESULT
        OUTPUT_VARIABLE OBJDUMP_OUTPUT
        ERROR_VARIABLE OBJDUMP_ERROR)

if (NOT OBJDUMP_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to inspect ${INPUT_LIBRARY}: ${OBJDUMP_ERROR}")
endif ()

string(REGEX MATCH "SONAME[ \t]+([^ \n]+)" SONAME_MATCH "${OBJDUMP_OUTPUT}")

if (CMAKE_MATCH_1)
    set(SONAME "${CMAKE_MATCH_1}")
else ()
    get_filename_component(SONAME "${INPUT_LIBRARY}" NAME)
endif ()

file(COPY_FILE "${INPUT_LIBRARY}" "${TARGET_DIR}/${SONAME}" ONLY_IF_DIFFERENT)
message(STATUS "Bundled runtime library ${SONAME}")

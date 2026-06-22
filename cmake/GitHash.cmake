# Writes ${OUT_FILE} with the current short git hash as STAYPUTVR_GIT_HASH.
# Invoked both at configure time and as a build-time custom target so the
# value shown in the UI tracks the checked-out commit. Only rewrites the file
# when the hash actually changes, to avoid needless rebuilds.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${SRC_DIR}"
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()

set(NEW_CONTENT "#pragma once\n#define STAYPUTVR_GIT_HASH \"${GIT_HASH}\"\n")

if(EXISTS "${OUT_FILE}")
    file(READ "${OUT_FILE}" OLD_CONTENT)
    if("${OLD_CONTENT}" STREQUAL "${NEW_CONTENT}")
        return()
    endif()
endif()

file(WRITE "${OUT_FILE}" "${NEW_CONTENT}")

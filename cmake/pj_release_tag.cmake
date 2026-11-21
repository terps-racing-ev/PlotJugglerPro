# Writes a header with the release tag this build came from, for the GitHub
# update check. Runs on every build (not just configure) so a tag created after
# configuring is still picked up. The header is only rewritten when the tag
# changes, so it does not force a rebuild otherwise.
#
# Inputs: SOURCE_DIR, OUTPUT_FILE

set(release_tag "")

find_package(Git QUIET)
if(GIT_FOUND)
  # Release tags are date-based (e.g. 2026.10.26). --abbrev=0 gives the nearest
  # tag, so a build a few commits past a release reports that release.
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0 --match "[0-9]*"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE release_tag
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE git_result)
  if(NOT git_result EQUAL 0)
    set(release_tag "")
  endif()
endif()

set(content "#pragma once\n#define PJ_RELEASE_TAG \"${release_tag}\"\n")

set(old_content "")
if(EXISTS "${OUTPUT_FILE}")
  file(READ "${OUTPUT_FILE}" old_content)
endif()

if(NOT old_content STREQUAL content)
  file(WRITE "${OUTPUT_FILE}" "${content}")
endif()

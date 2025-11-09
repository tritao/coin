if(NOT DEFINED COIN_VISUAL_TESTS_EXECUTABLE OR
   NOT DEFINED COIN_VISUAL_TEST_SPEC_DIR OR
   NOT DEFINED COIN_VISUAL_TEST_WORK_DIR)
  message(FATAL_ERROR "Missing CoinVisualTests --only contract arguments")
endif()

file(MAKE_DIRECTORY "${COIN_VISUAL_TEST_WORK_DIR}")

execute_process(
  COMMAND "${COIN_VISUAL_TESTS_EXECUTABLE}" run
          --spec-dir "${COIN_VISUAL_TEST_SPEC_DIR}"
          --artifacts-dir "${COIN_VISUAL_TEST_WORK_DIR}/artifacts"
          --only "__missing_visual_spec__"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

if(result EQUAL 0)
  message(FATAL_ERROR
    "An unknown --only id incorrectly reported success\n"
    "stdout: ${output}\nstderr: ${error}")
endif()

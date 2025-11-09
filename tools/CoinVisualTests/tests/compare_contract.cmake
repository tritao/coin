if(NOT DEFINED COIN_VISUAL_TESTS_EXECUTABLE OR
   NOT DEFINED COIN_VISUAL_TEST_EXPECTED OR
   NOT DEFINED COIN_VISUAL_TEST_SAME_SIZE OR
   NOT DEFINED COIN_VISUAL_TEST_DIFFERENT_SIZE OR
   NOT DEFINED COIN_VISUAL_TEST_WORK_DIR)
  message(FATAL_ERROR "Missing CoinVisualTests comparator contract arguments")
endif()

file(MAKE_DIRECTORY "${COIN_VISUAL_TEST_WORK_DIR}")
set(same_diff "${COIN_VISUAL_TEST_WORK_DIR}/same.diff.png")
set(same_metrics "${COIN_VISUAL_TEST_WORK_DIR}/same.metrics.json")
set(different_diff "${COIN_VISUAL_TEST_WORK_DIR}/different.diff.png")
set(different_metrics "${COIN_VISUAL_TEST_WORK_DIR}/different.metrics.json")
file(REMOVE "${same_diff}" "${same_metrics}" "${different_diff}" "${different_metrics}")

execute_process(
  COMMAND "${COIN_VISUAL_TESTS_EXECUTABLE}" compare
          --expected "${COIN_VISUAL_TEST_EXPECTED}"
          --actual "${COIN_VISUAL_TEST_SAME_SIZE}"
          --diff "${same_diff}"
          --metrics "${same_metrics}"
  RESULT_VARIABLE same_result
  OUTPUT_VARIABLE same_output
  ERROR_VARIABLE same_error)
if(NOT same_result EQUAL 1)
  message(FATAL_ERROR
    "Same-size mismatch returned ${same_result}, expected 1\n"
    "stdout: ${same_output}\nstderr: ${same_error}")
endif()
if(NOT EXISTS "${same_diff}" OR NOT EXISTS "${same_metrics}")
  message(FATAL_ERROR "Same-size mismatch did not produce diff and metrics artifacts")
endif()

execute_process(
  COMMAND "${COIN_VISUAL_TESTS_EXECUTABLE}" compare
          --expected "${COIN_VISUAL_TEST_EXPECTED}"
          --actual "${COIN_VISUAL_TEST_DIFFERENT_SIZE}"
          --diff "${different_diff}"
          --metrics "${different_metrics}"
  RESULT_VARIABLE different_result
  OUTPUT_VARIABLE different_output
  ERROR_VARIABLE different_error)
if(NOT different_result EQUAL 1)
  message(FATAL_ERROR
    "Dimension mismatch returned ${different_result}, expected 1\n"
    "stdout: ${different_output}\nstderr: ${different_error}")
endif()
if(EXISTS "${different_diff}")
  message(FATAL_ERROR "Dimension mismatch must not produce a fabricated diff image")
endif()
if(NOT EXISTS "${different_metrics}")
  message(FATAL_ERROR "Dimension mismatch did not produce metrics")
endif()
file(READ "${different_metrics}" different_metrics_text)
if(NOT different_metrics_text MATCHES "\\\"dimensions_match\\\": false")
  message(FATAL_ERROR "Dimension mismatch metrics did not record dimensions_match=false")
endif()

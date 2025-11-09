if(NOT DEFINED COIN_VISUAL_TESTS_EXECUTABLE OR
   NOT DEFINED COIN_VISUAL_TEST_SCENE OR
   NOT DEFINED COIN_VISUAL_TEST_WORK_DIR)
  message(FATAL_ERROR "Missing CoinVisualTests baseline contract arguments")
endif()

file(REMOVE_RECURSE "${COIN_VISUAL_TEST_WORK_DIR}")
file(MAKE_DIRECTORY
  "${COIN_VISUAL_TEST_WORK_DIR}/specs"
  "${COIN_VISUAL_TEST_WORK_DIR}/baseline-target")
file(TO_CMAKE_PATH "${COIN_VISUAL_TEST_SCENE}" scene_path)
file(TO_CMAKE_PATH "${COIN_VISUAL_TEST_WORK_DIR}/baseline-target" baseline_path)
file(WRITE "${COIN_VISUAL_TEST_WORK_DIR}/specs/baseline_failure.yml"
  "id: baseline_failure\n"
  "scene: ${scene_path}\n"
  "viewport:\n"
  "  width: 64\n"
  "  height: 64\n"
  "  background: [0.2, 0.2, 0.2, 1.0]\n"
  "baseline: ${baseline_path}\n"
  "compare: default\n")

execute_process(
  COMMAND "${COIN_VISUAL_TESTS_EXECUTABLE}" run
          --spec-dir "${COIN_VISUAL_TEST_WORK_DIR}/specs"
          --artifacts-dir "${COIN_VISUAL_TEST_WORK_DIR}/artifacts"
          --update-baselines
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result EQUAL 0)
  message(FATAL_ERROR
    "An unwritable baseline destination incorrectly reported success\n"
    "stdout: ${output}\nstderr: ${error}")
endif()

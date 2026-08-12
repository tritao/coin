if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE OR
   NOT DEFINED TEMPLATE_FILE OR NOT DEFINED COIN_HEADER_DEF OR
   NOT DEFINED COIN_TEXTVAR_NAME)
  message(FATAL_ERROR "GenerateTextHeader.cmake requires input, output, template, and symbol arguments")
endif()

get_filename_component(_output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")

file(READ "${INPUT_FILE}" f0)
string(REGEX REPLACE "\\\\" "\\\\\\\\" f1 "${f0}")
string(REGEX REPLACE "\"" "\\\\\"" f2 "${f1}")
string(REGEX REPLACE "\r?\n" "\\\\n\"\n  \"" COIN_STR_SOURCE_CODE "${f2}")

configure_file("${TEMPLATE_FILE}" "${OUTPUT_FILE}" @ONLY)

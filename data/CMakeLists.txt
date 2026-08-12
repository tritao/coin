macro(strfy _input _postfix)
  get_filename_component(_basename "${_input}" NAME_WE)
  get_filename_component(_suffix "${_input}" EXT)
  get_filename_component(_path "${_input}" PATH)
  string(SUBSTRING ${_suffix} 1 -1 _suffix)
  string(TOUPPER "SO_${_basename}_${_suffix}_H" COIN_HEADER_DEF)
  string(TOUPPER ${_basename} COIN_TEXTVAR_NAME)
  set(COIN_TEXTVAR_NAME "${COIN_TEXTVAR_NAME}${_postfix}")
  set(_input_file "${PROJECT_SOURCE_DIR}/${_input}")
  set(_output_file "${CMAKE_CURRENT_BINARY_DIR}/${_path}/${_basename}.h")
  add_custom_command(
    OUTPUT "${_output_file}"
    COMMAND ${CMAKE_COMMAND}
      -DINPUT_FILE=${_input_file}
      -DOUTPUT_FILE=${_output_file}
      -DTEMPLATE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/strfytemplate.cmake.in
      -DCOIN_HEADER_DEF=${COIN_HEADER_DEF}
      -DCOIN_TEXTVAR_NAME=${COIN_TEXTVAR_NAME}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/GenerateTextHeader.cmake
    DEPENDS
      "${_input_file}"
      "${CMAKE_CURRENT_SOURCE_DIR}/GenerateTextHeader.cmake"
      "${CMAKE_CURRENT_SOURCE_DIR}/strfytemplate.cmake.in"
    VERBATIM
  )
  list(APPEND COIN_GENERATED_DATA_HEADERS "${_output_file}")
endmacro()

file(GLOB_RECURSE COIN_SHADER_GLSL RELATIVE ${PROJECT_SOURCE_DIR} *.glsl)
foreach(_input_file ${COIN_SHADER_GLSL})
  strfy(${_input_file} "_shadersource")
endforeach()
file(GLOB_RECURSE COIN_SHADER_INVENTOR RELATIVE ${PROJECT_SOURCE_DIR} *.iv)
foreach(_input_file ${COIN_SHADER_INVENTOR})
  strfy(${_input_file} "_draggergeometry")
endforeach()

# Keep generated text headers on the build graph.  The object libraries that
# include them depend on this target below, so editing a shader regenerates its
# embedded header before compilation without requiring a CMake reconfigure.
add_custom_target(coin_generated_data_headers
  DEPENDS ${COIN_GENERATED_DATA_HEADERS}
)

file(GLOB_RECURSE COMMON_SCXML ${CMAKE_CURRENT_SOURCE_DIR}/[cep]*.xml)
file(GLOB_RECURSE COMMON_DRAGGERDEFAULTS ${CMAKE_CURRENT_SOURCE_DIR}/*.iv)
file(GLOB_RECURSE COMMON_SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.glsl)

set(COMMON_RESOURCES ${COMMON_SCXML} ${COMMON_DRAGGERDEFAULTS} ${COMMON_SHADERS} PARENT_SCOPE)

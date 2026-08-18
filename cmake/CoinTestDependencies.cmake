include_guard(GLOBAL)

include(FetchContent)

# Test-only dependency revisions. Keep these as full commit IDs so an
# explicitly enabled fallback remains reproducible even when upstream tags
# move or are recreated.
set(COIN_TEST_GLFW_COMMIT
  7b6aead9fb88b3623e3b3725ebb42670cbe4c579
  CACHE INTERNAL "Pinned GLFW commit for test dependency fallback")
set(COIN_TEST_YAML_CPP_COMMIT
  f7320141120f720aecc4c32be25586e7da9eb978
  CACHE INTERNAL "Pinned yaml-cpp commit for test dependency fallback")

function(coin_resolve_test_glfw)
  set(_coin_test_glfw_target "")

  if(TARGET glfw)
    set(_coin_test_glfw_target glfw)
  elseif(TARGET glfw3::glfw)
    set(_coin_test_glfw_target glfw3::glfw)
  else()
    find_package(glfw3 3.3 QUIET)
    if(TARGET glfw)
      set(_coin_test_glfw_target glfw)
    elseif(TARGET glfw3::glfw)
      set(_coin_test_glfw_target glfw3::glfw)
    endif()
  endif()

  if(NOT _coin_test_glfw_target AND COIN_FETCH_TEST_DEPENDENCIES)
    message(STATUS
      "GLFW was not found; fetching pinned test dependency ${COIN_TEST_GLFW_COMMIT}")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "Disable GLFW documentation" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Disable GLFW examples" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "Disable GLFW tests" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "Disable GLFW installation" FORCE)
    FetchContent_Declare(coin_test_glfw
      GIT_REPOSITORY https://github.com/glfw/glfw.git
      GIT_TAG ${COIN_TEST_GLFW_COMMIT})
    FetchContent_MakeAvailable(coin_test_glfw)
    if(TARGET glfw)
      set(_coin_test_glfw_target glfw)
    elseif(TARGET glfw3::glfw)
      set(_coin_test_glfw_target glfw3::glfw)
    endif()
  endif()

  set(COIN_TEST_GLFW_TARGET "${_coin_test_glfw_target}" PARENT_SCOPE)
endfunction()

function(coin_resolve_test_yaml_cpp)
  set(_coin_test_yaml_target "")

  if(TARGET yaml-cpp::yaml-cpp)
    set(_coin_test_yaml_target yaml-cpp::yaml-cpp)
  elseif(TARGET yaml-cpp)
    set(_coin_test_yaml_target yaml-cpp)
  else()
    find_package(yaml-cpp QUIET)
    if(TARGET yaml-cpp::yaml-cpp)
      set(_coin_test_yaml_target yaml-cpp::yaml-cpp)
    elseif(TARGET yaml-cpp)
      set(_coin_test_yaml_target yaml-cpp)
    endif()
  endif()

  if(NOT _coin_test_yaml_target AND COIN_FETCH_TEST_DEPENDENCIES)
    message(STATUS
      "yaml-cpp was not found; fetching pinned test dependency ${COIN_TEST_YAML_CPP_COMMIT}")
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
    set(YAML_CPP_INSTALL OFF CACHE BOOL "Disable yaml-cpp installation" FORCE)
    FetchContent_Declare(coin_test_yaml_cpp
      GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
      GIT_TAG ${COIN_TEST_YAML_CPP_COMMIT})
    FetchContent_MakeAvailable(coin_test_yaml_cpp)
    if(TARGET yaml-cpp::yaml-cpp)
      set(_coin_test_yaml_target yaml-cpp::yaml-cpp)
    elseif(TARGET yaml-cpp)
      set(_coin_test_yaml_target yaml-cpp)
    endif()
  endif()

  set(COIN_TEST_YAML_TARGET "${_coin_test_yaml_target}" PARENT_SCOPE)
endfunction()

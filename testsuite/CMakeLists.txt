macro(create_testsuite input)
	get_filename_component(FLNAME "${input}" NAME_WE)
	get_filename_component(FLPATH "${input}" PATH)
	string(REGEX REPLACE ".*[/\\]" "" FLSUBFLD "${FLPATH}")
	set(COIN_STR_TEST_CLASS "${FLNAME}")
	file(READ ${PROJECT_SOURCE_DIR}/${input} f0)
	if(f0 MATCHES "#ifdef[ \t]+COIN_TEST_SUITE")
		# message(STATUS "Parse: ${PROJECT_SOURCE_DIR}/${input} - ${FLPATHSUB}${FLNAME}Test.cpp")
		# get first include from file, which we assume is include to tested class
		string(REGEX MATCH "[\n\r]+#include[ \t]<[^\n]+" iclass "${f0}")
		# get block between '#ifdef COIN_TEST_SUITE' and '#endif'
		string(REGEX REPLACE ".*#ifdef[ \t]+COIN_TEST_SUITE" "" f1 "${f0}")
		string(REGEX REPLACE "#endif[ \t/!]+COIN_TEST_SUITE.*" "" f2 "${f1}")
		# get all #include statements within COIN_TEST_SUITE code block
		string(REGEX MATCHALL "#include[ \t]<[^\n]+" i0 "${f2}")
		string(REPLACE ";" "\n" COIN_STR_TEST_INCL "${i0}")
		set(COIN_STR_TEST_INCL "${iclass}\n${COIN_STR_TEST_INCL}")
		# remove #include statements from test code string (moved to ${COIN_STR_TEST_INCL})
		string(REGEX REPLACE "[\n\r ]*#include[ \t]<[^\n]+" "" COIN_STR_TEST_CODE "${f2}")
		# generate new test code file with extracted snippets
		configure_file(TestSuiteTemplate.cmake.in "${FLSUBFLD}${FLNAME}Test.cpp")
	endif()
endmacro()

macro(create_file_test input testfunc filter)
	file(GLOB_RECURSE COIN_IV_FILES RELATIVE ${PROJECT_SOURCE_DIR}
		../${input}/*.iv
		../${input}/*.iv.gz
	)
	if (HAVE_VRML97)
		file(GLOB_RECURSE COIN_WRL_VRML_FILES RELATIVE ${PROJECT_SOURCE_DIR}
			../${input}/*.wrl
			../${input}/*.vrml
			../${input}/*.wrl.gz
			../${input}/*.vrml.gz
		)
		list(APPEND COIN_IV_FILES ${COIN_WRL_VRML_FILES})
	endif()
	string(REGEX REPLACE "[\\./\\-]+" "" TESTSUITENAME "${input}")
	set(COIN_STR_TEST_INCL "")
	set(COIN_STR_TEST_CODE "const char *readError[] = { \"Coin read error:\", 0 };\n")
	set(COIN_STR_TEST_CLASS "${TESTSUITENAME}")
	set(extra_macro_args ${ARGN})
	list(LENGTH extra_macro_args num_extra_args)
	foreach(INPUTFILE ${COIN_IV_FILES})
		if(${num_extra_args} EQUAL 0 OR NOT ("${INPUTFILE}" MATCHES "${extra_macro_args}"))
			string(REGEX REPLACE "[\\./\\-]+" "_" TESTNAME "${INPUTFILE}")
			if (${filter})
				set(COIN_STR_TEST_CODE "${COIN_STR_TEST_CODE}\nBOOST_AUTO_TEST_CASE(${TESTNAME}) { PushMessageSuppressFilters(readError); ResetReadErrorCount(); test_file(\"${PROJECT_SOURCE_DIR}/${INPUTFILE}\", &${testfunc}); PopMessageSuppressFilters(); }")
			else()
				set(COIN_STR_TEST_CODE "${COIN_STR_TEST_CODE}\nBOOST_AUTO_TEST_CASE(${TESTNAME}) { ResetReadErrorCount(); test_file(\"${PROJECT_SOURCE_DIR}/${INPUTFILE}\", &${testfunc}); }")
			endif()
		endif()
	endforeach()
	configure_file(TestSuiteTemplate.cmake.in "${TESTSUITENAME}Test.cpp")
endmacro()

# Parse all source files for embedded '#ifdef COIN_TEST_SUITE' and extract those blocks
# to separate test source files.
file(GLOB_RECURSE COIN_SRC_FILES RELATIVE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/*.cpp)
foreach(INPUTFILE ${COIN_SRC_FILES})
	create_testsuite(${INPUTFILE})
endforeach()

# Create test cases for all scenes under models folder (this replaces StandardTests.cpp).
# The original StandardTests.cpp also tested additional folders:
# - "killers" with testInCorrectFile
# - "slackers" with testOutOfSpecFile
# However, those folders are not in the repository.
create_file_test("models" "testCorrectFile" false "incorrect")
# bug039.iv and bug055-5.iv contain errors but load anyway: ignore for now
create_file_test("models/io/incorrect" "testIncorrectFile" true "(bug039|bug055-5)")

# Include all extracted '*Tests.cpp' files in the target.
FILE(GLOB COIN_TEST_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/*Test.cpp")

add_executable(CoinTests TestSuiteMain.cpp TestSuiteUtils.cpp TestSuiteMisc.cpp ${COIN_TEST_SOURCES})
set_target_properties(CoinTests PROPERTIES DEBUG_POSTFIX "${CMAKE_DEBUG_POSTFIX}")
target_link_libraries(CoinTests Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(CoinTests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/include/Inventor/annex
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
if (USE_PTHREAD)
	target_link_libraries(CoinTests pthread)
endif()
add_test(NAME CoinTests COMMAND CoinTests)

add_library(CoinTestInternals INTERFACE)
target_compile_definitions(CoinTestInternals INTERFACE COIN_INTERNAL)
target_include_directories(CoinTestInternals INTERFACE ${PROJECT_SOURCE_DIR}/src)

set(COIN_BUILD_GL_TESTS "AUTO" CACHE STRING
	"Build generic GLFW OpenGL integration tests (AUTO, ON, or OFF).")
set_property(CACHE COIN_BUILD_GL_TESTS PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${COIN_BUILD_GL_TESTS}" COIN_BUILD_GL_TESTS)
if(NOT COIN_BUILD_GL_TESTS STREQUAL "AUTO" AND
	NOT COIN_BUILD_GL_TESTS STREQUAL "ON" AND
	NOT COIN_BUILD_GL_TESTS STREQUAL "OFF")
	message(FATAL_ERROR "COIN_BUILD_GL_TESTS must be AUTO, ON, or OFF")
endif()

set(COIN_BUILD_GL_TESTS_EFFECTIVE OFF)
if(NOT COIN_BUILD_GL_TESTS STREQUAL "OFF")
	include(CoinTestDependencies)
	coin_resolve_test_glfw()
	if(COIN_TEST_GLFW_TARGET)
		set(COIN_BUILD_GL_TESTS_EFFECTIVE ON)
	elseif(COIN_BUILD_GL_TESTS STREQUAL "ON")
		message(FATAL_ERROR
			"COIN_BUILD_GL_TESTS=ON requires GLFW 3.3 or newer; install it or enable COIN_FETCH_TEST_DEPENDENCIES=ON")
	else()
		message(STATUS "GLFW not found; generic OpenGL tests are disabled")
	endif()
endif()

if(COIN_BUILD_GL_TESTS_EFFECTIVE)
	add_library(CoinGLTestSupport STATIC
		support/GLTestContext.cpp
		support/GLTestFramebuffer.cpp)
	target_include_directories(CoinGLTestSupport PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/support
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	target_compile_definitions(CoinGLTestSupport PUBLIC GLFW_INCLUDE_NONE)
	target_link_libraries(CoinGLTestSupport PUBLIC
		${COIN_TEST_GLFW_TARGET}
		${COIN_TARGET_LINK_LIBRARIES})
	target_link_libraries(CoinGLTestSupport PRIVATE Coin)

	# Compile the private readback implementation into the white-box test so
	# the test does not depend on a non-public symbol crossing Coin's DLL ABI.
	add_library(CoinGLReadbackTestSupport STATIC
		${PROJECT_SOURCE_DIR}/src/rendering/CoinGLReadback.cpp)
	target_link_libraries(CoinGLReadbackTestSupport PRIVATE CoinTestInternals Coin
		${COIN_TARGET_LINK_LIBRARIES})
	target_include_directories(CoinGLReadbackTestSupport PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})

	add_executable(GLSLRuntimeTest GLSLRuntimeTest.cpp)
	target_link_libraries(GLSLRuntimeTest PRIVATE
		CoinGLTestSupport CoinTestInternals Coin ${COIN_TARGET_LINK_LIBRARIES})
	target_include_directories(GLSLRuntimeTest PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME GLSLRuntimeTest COMMAND GLSLRuntimeTest)
	set_tests_properties(GLSLRuntimeTest PROPERTIES
		SKIP_RETURN_CODE 77 LABELS "gl-core")

	add_executable(GLSLDiagnosticsTest GLSLDiagnosticsTest.cpp)
	target_link_libraries(GLSLDiagnosticsTest PRIVATE
		CoinGLTestSupport CoinTestInternals Coin ${COIN_TARGET_LINK_LIBRARIES})
	target_include_directories(GLSLDiagnosticsTest PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME GLSLDiagnosticsTest COMMAND GLSLDiagnosticsTest)
	set_tests_properties(GLSLDiagnosticsTest PROPERTIES
		SKIP_RETURN_CODE 77 LABELS "gl-core")

	add_executable(OffscreenReadbackTest OffscreenReadbackTest.cpp)
	target_link_libraries(OffscreenReadbackTest PRIVATE
		CoinGLTestSupport CoinGLReadbackTestSupport CoinTestInternals Coin
		${COIN_TARGET_LINK_LIBRARIES})
	target_include_directories(OffscreenReadbackTest PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME OffscreenReadbackTest COMMAND OffscreenReadbackTest)
	set_tests_properties(OffscreenReadbackTest PROPERTIES
		SKIP_RETURN_CODE 77 LABELS "gl-core")
endif()

if(HAVE_EGL)
	add_executable(EGLBindingTest EGLBindingTest.cpp)
	target_link_libraries(EGLBindingTest CoinTestInternals Coin ${COIN_TARGET_LINK_LIBRARIES})
	target_include_directories(EGLBindingTest PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME EGLBindingTest COMMAND EGLBindingTest)
	set_tests_properties(EGLBindingTest PROPERTIES SKIP_RETURN_CODE 77)

	add_executable(OffscreenContextTest OffscreenContextTest.cpp)
	target_link_libraries(OffscreenContextTest PRIVATE
		CoinTestInternals Coin ${COIN_TARGET_LINK_LIBRARIES})
	target_include_directories(OffscreenContextTest PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME OffscreenContextTest COMMAND OffscreenContextTest)
	set_tests_properties(OffscreenContextTest PROPERTIES
		SKIP_RETURN_CODE 77 LABELS "egl;offscreen;requires-egl")
endif()

# Many warnings are generated from test macros on macOS with Xcode.
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Wtautological-compare DISABLE_WARNING_TAUTOLOGCOMPARE)
if(DISABLE_WARNING_TAUTOLOGCOMPARE)
	target_compile_options(CoinTests PUBLIC -Wno-tautological-compare)
endif()

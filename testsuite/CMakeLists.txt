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
if(COIN_BUILD_LEGACY_GL_RENDERER)
	create_file_test("models" "testCorrectFile" false "incorrect")
else()
	# ExtSelection is part of the legacy renderer and is not registered in
	# core-only builds, so do not include scenes that require that node.
	create_file_test("models" "testCorrectFile" false "(incorrect|models/dead_simple/all-iv\\.iv$)")
endif()
# bug039.iv and bug055-5.iv contain errors but load anyway: ignore for now
create_file_test("models/io/incorrect" "testIncorrectFile" true "(bug039|bug055-5)")

# Include all extracted '*Tests.cpp' files in the target.
FILE(GLOB COIN_TEST_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/*Test.cpp")
if(NOT COIN_BUILD_LEGACY_GL_RENDERER)
  list(FILTER COIN_TEST_SOURCES EXCLUDE REGEX "/shadowsSo.*Test\\.cpp$")
endif()

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
set_tests_properties(CoinTests PROPERTIES LABELS "unit")

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

add_library(CoinTestInternals INTERFACE)
target_compile_definitions(CoinTestInternals INTERFACE COIN_INTERNAL)
target_include_directories(CoinTestInternals INTERFACE ${PROJECT_SOURCE_DIR}/src)

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

	function(coin_add_gl_test)
		cmake_parse_arguments(ARG "" "NAME;PROFILE" "SOURCES;LABELS" ${ARGN})
		if(NOT ARG_NAME OR NOT ARG_PROFILE OR NOT ARG_SOURCES)
			message(FATAL_ERROR "coin_add_gl_test requires NAME, PROFILE, and SOURCES")
		endif()
		string(TOLOWER "${ARG_PROFILE}" profile)
		if(NOT profile STREQUAL "core" AND NOT profile STREQUAL "compat")
			message(FATAL_ERROR "GL test PROFILE must be core or compat")
		endif()
		add_executable(${ARG_NAME} ${ARG_SOURCES})
		target_link_libraries(${ARG_NAME} CoinGLTestSupport CoinTestInternals Coin
			${COIN_TARGET_LINK_LIBRARIES})
		target_include_directories(${ARG_NAME} PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/support
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_BINARY_DIR}/include
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
		set(test_labels "gl-${profile}")
		if(ARG_LABELS)
			list(APPEND test_labels ${ARG_LABELS})
		endif()
		set_tests_properties(${ARG_NAME} PROPERTIES
			SKIP_RETURN_CODE 77 LABELS "${test_labels}")
	endfunction()

	coin_add_gl_test(NAME GLSLRuntimeTest PROFILE core
		SOURCES GLSLRuntimeTest.cpp)
	coin_add_gl_test(NAME GLSLDiagnosticsTest PROFILE core
		SOURCES GLSLDiagnosticsTest.cpp)
	coin_add_gl_test(NAME OffscreenReadbackTest PROFILE core
		SOURCES OffscreenReadbackTest.cpp)
	target_link_libraries(OffscreenReadbackTest CoinGLReadbackTestSupport)
	coin_add_gl_test(NAME DrawListGLTest PROFILE core
		SOURCES DrawListGLTest.cpp)
	coin_add_gl_test(NAME RetainedMaterialLightingGLTest PROFILE core
		SOURCES RetainedMaterialLightingGLTest.cpp)
	coin_add_gl_test(NAME RetainedRasterGLTest PROFILE core
		SOURCES RetainedRasterGLTest.cpp)
	coin_add_gl_test(NAME RetainedSoText2RasterLayoutTest PROFILE core
		SOURCES RetainedSoText2RasterLayoutTest.cpp)
	coin_add_gl_test(NAME DrawListPickingTest PROFILE core
		SOURCES DrawListPickingTest.cpp)
	coin_add_gl_test(NAME DrawListManagerTest PROFILE core
		SOURCES DrawListManagerTest.cpp)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		coin_add_gl_test(NAME LegacySoText2RasterLayoutTest PROFILE compat
			SOURCES LegacySoText2RasterLayoutTest.cpp LABELS requires-legacygl)
		coin_add_gl_test(NAME LegacyDirectRasterTransparencyTest PROFILE compat
			SOURCES LegacyDirectRasterTransparencyTest.cpp LABELS requires-legacygl)
		coin_add_gl_test(NAME LegacyShapeGatingOrderTest PROFILE compat
			SOURCES LegacyShapeGatingOrderTest.cpp LABELS requires-legacygl)
	endif()
endif()

function(coin_add_egl_test)
	cmake_parse_arguments(ARG "" "NAME" "SOURCES;LABELS" ${ARGN})
	if(NOT ARG_NAME OR NOT ARG_SOURCES)
		message(FATAL_ERROR "coin_add_egl_test requires NAME and SOURCES")
	endif()
	add_executable(${ARG_NAME} ${ARG_SOURCES})
	target_link_libraries(${ARG_NAME} CoinTestInternals Coin
		${COIN_TARGET_LINK_LIBRARIES})
	target_include_directories(${ARG_NAME} PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
	set(test_labels "egl")
	if(ARG_LABELS)
		list(APPEND test_labels ${ARG_LABELS})
	endif()
	set_tests_properties(${ARG_NAME} PROPERTIES
		SKIP_RETURN_CODE 77 LABELS "${test_labels}")
endfunction()

add_test(
	NAME ShaderGeneratorTest
	COMMAND "${CMAKE_COMMAND}"
		-DCOIN_SOURCE_DIR=${PROJECT_SOURCE_DIR}
		-DCOIN_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
		"-DCOIN_GENERATOR=${CMAKE_GENERATOR}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/shader-generator-test.cmake"
)

add_executable(CoreTraversalTest CoreTraversalTest.cpp)
target_link_libraries(CoreTraversalTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(CoreTraversalTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME CoreTraversalTest COMMAND CoreTraversalTest)

add_executable(LegacyGLABITest LegacyGLABITest.cpp)
target_link_libraries(LegacyGLABITest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(LegacyGLABITest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME LegacyGLABITest COMMAND LegacyGLABITest)

if(NOT COIN_BUILD_LEGACY_GL_RENDERER)
	add_executable(SceneManagerCoreTest SceneManagerCoreTest.cpp)
	target_link_libraries(SceneManagerCoreTest Coin ${COIN_TARGET_LINK_LIBRARIES})
	target_include_directories(SceneManagerCoreTest PRIVATE
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES}
	)
	add_test(NAME SceneManagerCoreTest COMMAND SceneManagerCoreTest)
endif()

	add_executable(LegacyBlendingTest LegacyBlendingTest.cpp)
target_link_libraries(LegacyBlendingTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(LegacyBlendingTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME LegacyBlendingTest COMMAND LegacyBlendingTest)

if(COIN_BUILD_LEGACY_GL_RENDERER)
	coin_add_gl_test(
		NAME DevicePixelRatioTraversalTest
		PROFILE compat
		SOURCES DevicePixelRatioTraversalTest.cpp)
endif()
add_executable(RetainedIRTest RetainedIRTest.cpp)
target_compile_definitions(RetainedIRTest PRIVATE COIN_ALLOW_PRIVATE_HEADERS)
target_link_libraries(RetainedIRTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RetainedIRTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RetainedIRTest COMMAND RetainedIRTest)

add_executable(RenderPlanTest RenderPlanTest.cpp)
target_link_libraries(RenderPlanTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RenderPlanTest PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RenderPlanTest COMMAND RenderPlanTest)

add_executable(RetainedNodeTest RetainedNodeTest.cpp)
target_compile_definitions(RetainedNodeTest PRIVATE COIN_ALLOW_PRIVATE_HEADERS)
target_link_libraries(RetainedNodeTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RetainedNodeTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RetainedNodeTest COMMAND RetainedNodeTest)

add_executable(RetainedMixedTopologyTest RetainedMixedTopologyTest.cpp)
target_link_libraries(RetainedMixedTopologyTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RetainedMixedTopologyTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_executable(RetainedMaterialLightingTest RetainedMaterialLightingTest.cpp)
target_link_libraries(RetainedMaterialLightingTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RetainedMaterialLightingTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RetainedMixedTopologyTest COMMAND RetainedMixedTopologyTest)
add_test(NAME RetainedMaterialLightingTest COMMAND RetainedMaterialLightingTest)
add_executable(SoText2RasterTest RetainedRasterTextTest.cpp)
target_link_libraries(SoText2RasterTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(SoText2RasterTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES})
add_test(NAME SoText2RasterTest COMMAND SoText2RasterTest)

add_executable(RenderManagerPipelineTest RenderManagerPipelineTest.cpp)
target_link_libraries(RenderManagerPipelineTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RenderManagerPipelineTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES})
add_test(NAME RenderManagerPipelineTest COMMAND RenderManagerPipelineTest)
set_tests_properties(RenderManagerPipelineTest PROPERTIES LABELS "unit")

if(HAVE_EGL)
	coin_add_egl_test(NAME EGLBindingTest SOURCES EGLBindingTest.cpp
		LABELS requires-egl)
	coin_add_egl_test(NAME OffscreenContextTest SOURCES OffscreenContextTest.cpp
		LABELS offscreen requires-egl)
endif()

# Many warnings are generated from test macros on macOS with Xcode.
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Wtautological-compare DISABLE_WARNING_TAUTOLOGCOMPARE)
if(DISABLE_WARNING_TAUTOLOGCOMPARE)
	target_compile_options(CoinTests PUBLIC -Wno-tautological-compare)
endif()

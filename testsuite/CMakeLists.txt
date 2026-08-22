set(COIN_TEST_SKIP_RETURN_CODE 77)

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

# Generic OpenGL tests use the existing GLFW test support.  EGL remains an
# explicit implementation-contract suite below; it is not the provider for
# ordinary GL semantics tests.
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
set(COIN_GLFW_TARGET "")
if(NOT COIN_BUILD_GL_TESTS STREQUAL "OFF")
	include(CoinTestDependencies)
	coin_resolve_test_glfw()
	set(COIN_GLFW_TARGET "${COIN_TEST_GLFW_TARGET}")
	if(COIN_GLFW_TARGET)
		set(COIN_BUILD_GL_TESTS_EFFECTIVE ON)
	elseif(COIN_BUILD_GL_TESTS STREQUAL "ON")
		if(COIN_FETCH_TEST_DEPENDENCIES)
			message(FATAL_ERROR
				"COIN_BUILD_GL_TESTS=ON could not resolve GLFW after fetching the pinned dependency")
		else()
			message(FATAL_ERROR
				"COIN_BUILD_GL_TESTS=ON requires GLFW 3.3 or newer; install it or enable COIN_FETCH_TEST_DEPENDENCIES=ON")
		endif()
	else()
		message(STATUS
			"GLFW not found; generic OpenGL tests are disabled (set COIN_FETCH_TEST_DEPENDENCIES=ON to fetch it)")
	endif()
endif()

if(WIN32 AND COIN_BUILD_SHARED_LIBS AND COIN_BUILD_GL_TESTS_EFFECTIVE)
	message(STATUS
		"Generic OpenGL tests are disabled for Windows shared builds because retained implementation symbols are not exported")
	set(COIN_BUILD_GL_TESTS_EFFECTIVE OFF)
endif()

if(COIN_BUILD_GL_TESTS_EFFECTIVE)
	add_library(CoinGLTestSupport STATIC
		support/GLRenderTestSession.cpp
		support/GLTestContext.cpp
		support/GLTestFramebuffer.cpp)
	target_include_directories(CoinGLTestSupport PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/support
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	target_link_libraries(CoinGLTestSupport PUBLIC
		${COIN_GLFW_TARGET}
		${COIN_TARGET_LINK_LIBRARIES}
		PRIVATE Coin)

	function(coin_add_gl_test)
		cmake_parse_arguments(ARG "PRIVATE_COIN_API" "NAME;PROFILE"
			"SOURCES;LABELS" ${ARGN})
		if(NOT ARG_NAME OR NOT ARG_PROFILE OR NOT ARG_SOURCES)
			message(FATAL_ERROR "coin_add_gl_test requires NAME, PROFILE, and SOURCES")
		endif()
		string(TOLOWER "${ARG_PROFILE}" profile)
		if(NOT profile STREQUAL "core" AND NOT profile STREQUAL "compat")
			message(FATAL_ERROR "GL test PROFILE must be core or compat")
		endif()
		if(ARG_PRIVATE_COIN_API AND WIN32 AND COIN_BUILD_SHARED_LIBS)
			message(STATUS
				"Skipping ${ARG_NAME}: private Coin symbols are not exported by the Windows DLL")
			return()
		endif()
		add_executable(${ARG_NAME} ${ARG_SOURCES})
		target_link_libraries(${ARG_NAME} CoinGLTestSupport Coin
			${COIN_TARGET_LINK_LIBRARIES})
		if(ARG_PRIVATE_COIN_API)
			target_compile_definitions(${ARG_NAME} PRIVATE COIN_INTERNAL)
		endif()
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
			SKIP_RETURN_CODE ${COIN_TEST_SKIP_RETURN_CODE} LABELS "${test_labels}")
	endfunction()
endif()

function(coin_add_egl_test)
	cmake_parse_arguments(ARG "" "NAME" "SOURCES;LABELS" ${ARGN})
	if(NOT ARG_NAME OR NOT ARG_SOURCES)
		message(FATAL_ERROR "coin_add_egl_test requires NAME and SOURCES")
	endif()
	add_executable(${ARG_NAME} ${ARG_SOURCES})
	target_link_libraries(${ARG_NAME} Coin ${COIN_TARGET_LINK_LIBRARIES})
	target_compile_definitions(${ARG_NAME} PRIVATE COIN_INTERNAL)
	target_include_directories(${ARG_NAME} PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})
	add_test(NAME ${ARG_NAME} COMMAND ${ARG_NAME})
	set(test_labels "egl")
	if(ARG_LABELS)
		set(test_labels ${ARG_LABELS})
	endif()
	set_tests_properties(${ARG_NAME} PROPERTIES
		SKIP_RETURN_CODE ${COIN_TEST_SKIP_RETURN_CODE} LABELS "${test_labels}")
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

add_executable(RetainedIRTest RetainedIRTest.cpp)
target_link_libraries(RetainedIRTest Coin
	${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RetainedIRTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RetainedIRTest COMMAND RetainedIRTest)

add_executable(RenderPlanTest
	RenderPlanTest.cpp
	${PROJECT_SOURCE_DIR}/src/rendering/SoRenderPlan.cpp)
target_link_libraries(RenderPlanTest Coin ${COIN_TARGET_LINK_LIBRARIES})
if(NOT WIN32 OR NOT COIN_BUILD_SHARED_LIBS)
	target_compile_definitions(RenderPlanTest PRIVATE COIN_INTERNAL)
endif()
target_include_directories(RenderPlanTest PRIVATE
	${PROJECT_SOURCE_DIR}/src
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RenderPlanTest COMMAND RenderPlanTest)

add_executable(RetainedNodeTest RetainedNodeTest.cpp)
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

add_executable(RetainedRasterTextTest RetainedRasterTextTest.cpp)
target_link_libraries(RetainedRasterTextTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RetainedRasterTextTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RetainedRasterTextTest COMMAND RetainedRasterTextTest)

add_executable(RetainedReplayContextTest RetainedReplayContextTest.cpp)
target_link_libraries(RetainedReplayContextTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RetainedReplayContextTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RetainedReplayContextTest COMMAND RetainedReplayContextTest)

add_executable(RenderLayerSemanticsTest RenderLayerSemanticsTest.cpp)
target_link_libraries(RenderLayerSemanticsTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RenderLayerSemanticsTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RenderLayerSemanticsTest COMMAND RenderLayerSemanticsTest)

add_executable(RenderManagerPipelineTest RenderManagerPipelineTest.cpp)
target_link_libraries(RenderManagerPipelineTest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(RenderManagerPipelineTest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME RenderManagerPipelineTest COMMAND RenderManagerPipelineTest)

add_executable(LegacyGLABITest LegacyGLABITest.cpp)
target_link_libraries(LegacyGLABITest Coin ${COIN_TARGET_LINK_LIBRARIES})
target_include_directories(LegacyGLABITest PRIVATE
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_BINARY_DIR}/include
	${COIN_TARGET_INCLUDE_DIRECTORIES}
)
add_test(NAME LegacyGLABITest COMMAND LegacyGLABITest)

if(COIN_BUILD_GL_TESTS_EFFECTIVE)
	coin_add_gl_test(
		NAME RetainedMaterialLightingGLTest
		PROFILE core
		PRIVATE_COIN_API
		SOURCES RetainedMaterialLightingGLTest.cpp)
	coin_add_gl_test(
		NAME RetainedRasterGLTest
		PROFILE core
		PRIVATE_COIN_API
		SOURCES RetainedRasterGLTest.cpp)
	coin_add_gl_test(
		NAME DrawListGLTest
		PROFILE core
		PRIVATE_COIN_API
		SOURCES DrawListGLTest.cpp)
	coin_add_gl_test(
		NAME DrawListPickingTest
		PROFILE core
		PRIVATE_COIN_API
		SOURCES DrawListPickingTest.cpp)
	coin_add_gl_test(
		NAME DrawListManagerTest
		PROFILE core
		PRIVATE_COIN_API
		SOURCES DrawListManagerTest.cpp)
	coin_add_gl_test(
		NAME RenderLayerLifecycleTest
		PROFILE core
		PRIVATE_COIN_API
		SOURCES RenderLayerLifecycleTest.cpp)
	coin_add_gl_test(
		NAME GLSLRuntimeTest
		PROFILE core
		PRIVATE_COIN_API
		SOURCES GLSLRuntimeTest.cpp)
	coin_add_gl_test(
		NAME GLSLDiagnosticsTest
		PROFILE core
		PRIVATE_COIN_API
		SOURCES GLSLDiagnosticsTest.cpp)
	coin_add_gl_test(
		NAME OffscreenReadbackTest
		PROFILE core
		PRIVATE_COIN_API
		SOURCES OffscreenReadbackTest.cpp)
	if(COIN_BUILD_LEGACY_GL_RENDERER)
		coin_add_gl_test(
			NAME SoText2RasterLayoutTest
			PROFILE compat
			SOURCES SoText2RasterLayoutTest.cpp
			LABELS legacygl)
		coin_add_gl_test(
			NAME LegacyDirectRasterTransparencyTest
			PROFILE compat
			SOURCES LegacyDirectRasterTransparencyTest.cpp
			LABELS legacygl)
		coin_add_gl_test(
			NAME LegacyShapeGatingOrderTest
			PROFILE compat
			SOURCES LegacyShapeGatingOrderTest.cpp
			LABELS legacygl)
	endif()
	if(COIN_BUILD_BENCHMARKS)
		add_library(CoinRenderWorkloadSupport STATIC
			support/RenderWorkloads.cpp)
		target_link_libraries(CoinRenderWorkloadSupport PRIVATE Coin)
		target_include_directories(CoinRenderWorkloadSupport PUBLIC
			${CMAKE_CURRENT_SOURCE_DIR}/support
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_BINARY_DIR}/include
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		if(NOT WIN32 OR NOT COIN_BUILD_SHARED_LIBS)
			add_executable(CoinRenderGLBenchmarks CoinRenderGLBenchmarks.cpp)
			target_link_libraries(CoinRenderGLBenchmarks PRIVATE
				CoinRenderWorkloadSupport CoinGLTestSupport
				Coin
				${COIN_TARGET_LINK_LIBRARIES})
			target_compile_definitions(CoinRenderGLBenchmarks PRIVATE COIN_INTERNAL)
			target_include_directories(CoinRenderGLBenchmarks PRIVATE
				${CMAKE_CURRENT_SOURCE_DIR}/support
				${PROJECT_SOURCE_DIR}/src
				${PROJECT_SOURCE_DIR}/include
				${PROJECT_BINARY_DIR}/include
				${COIN_TARGET_INCLUDE_DIRECTORIES})
			add_test(NAME CoinRenderGLBenchmarksSmoke
				COMMAND CoinRenderGLBenchmarks --smoke)
			set_tests_properties(CoinRenderGLBenchmarksSmoke PROPERTIES
				SKIP_RETURN_CODE ${COIN_TEST_SKIP_RETURN_CODE}
				LABELS "benchmark;benchmark-gl;benchmark-render")
		endif()
		add_executable(RenderWorkloadParityTest RenderWorkloadParityTest.cpp)
		target_link_libraries(RenderWorkloadParityTest PRIVATE
			CoinRenderWorkloadSupport CoinGLTestSupport
			Coin ${COIN_TARGET_LINK_LIBRARIES})
		target_include_directories(RenderWorkloadParityTest PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/support
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_BINARY_DIR}/include
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME RenderWorkloadParityTest COMMAND RenderWorkloadParityTest)
		set_tests_properties(RenderWorkloadParityTest PROPERTIES
			SKIP_RETURN_CODE ${COIN_TEST_SKIP_RETURN_CODE}
			LABELS "gl-core;benchmark;benchmark-render;workload-parity")
		add_executable(CoinRenderWorkloadViewer CoinRenderWorkloadViewer.cpp)
		target_sources(CoinRenderWorkloadViewer PRIVATE
			support/RenderWorkloadViewerController.cpp)
		target_link_libraries(CoinRenderWorkloadViewer PRIVATE
			CoinRenderWorkloadSupport CoinGLTestSupport
			Coin ${COIN_TARGET_LINK_LIBRARIES})
		target_include_directories(CoinRenderWorkloadViewer PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/support
			${PROJECT_SOURCE_DIR}/src
			${PROJECT_SOURCE_DIR}/include
			${PROJECT_BINARY_DIR}/include
			${COIN_TARGET_INCLUDE_DIRECTORIES})
		add_test(NAME CoinRenderWorkloadViewerSmoke
			COMMAND CoinRenderWorkloadViewer --smoke)
		set_tests_properties(CoinRenderWorkloadViewerSmoke PROPERTIES
			SKIP_RETURN_CODE ${COIN_TEST_SKIP_RETURN_CODE}
			LABELS "benchmark;benchmark-gl;benchmark-render;viewer-smoke")
	endif()
endif()

if(HAVE_EGL)
	coin_add_egl_test(
		NAME EGLBindingTest
		SOURCES EGLBindingTest.cpp
		LABELS egl requires-egl)
	coin_add_egl_test(
		NAME OffscreenContextTest
		SOURCES OffscreenContextTest.cpp
		LABELS offscreen requires-egl)
endif()

if(COIN_BUILD_BENCHMARKS)
	add_executable(CoinRenderBenchmarks CoinRenderBenchmarks.cpp)
	target_link_libraries(CoinRenderBenchmarks PRIVATE
		Coin ${COIN_TARGET_LINK_LIBRARIES})
	target_include_directories(CoinRenderBenchmarks PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_SOURCE_DIR}/include
		${PROJECT_BINARY_DIR}/include
		${COIN_TARGET_INCLUDE_DIRECTORIES})

	add_test(NAME CoinRenderBenchmarksSmoke
		COMMAND CoinRenderBenchmarks --smoke)
	set_tests_properties(CoinRenderBenchmarksSmoke PROPERTIES
		LABELS "benchmark;benchmark-render;benchmark-picking;benchmark-selection")

	add_test(NAME CoinRenderLifecycleStress
		COMMAND CoinRenderBenchmarks --stress --frames 10000)
	set_tests_properties(CoinRenderLifecycleStress PROPERTIES
		LABELS "stress;stress-render;stress-picking;stress-lifecycle")
endif()

# Keep the ordinary non-GL tests in the unit capability without maintaining a
# second test-name manifest. Tests with an explicit rendering/EGL label keep
# their more specific capability classification.
get_property(COIN_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
foreach(COIN_REGISTERED_TEST IN LISTS COIN_REGISTERED_TESTS)
	get_property(COIN_HAS_LABELS TEST "${COIN_REGISTERED_TEST}" PROPERTY LABELS SET)
	if(NOT COIN_HAS_LABELS)
		set_tests_properties(${COIN_REGISTERED_TEST} PROPERTIES LABELS "unit")
	endif()
endforeach()

# Many warnings are generated from test macros on macOS with Xcode.
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Wtautological-compare DISABLE_WARNING_TAUTOLOGCOMPARE)
if(DISABLE_WARNING_TAUTOLOGCOMPARE)
	target_compile_options(CoinTests PUBLIC -Wno-tautological-compare)
endif()

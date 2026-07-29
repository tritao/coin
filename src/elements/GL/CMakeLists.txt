# source files
set(COIN_GLELEMENTS_FILES
	SoGLCacheContextElement.cpp
	SoGLClipPlaneElement.cpp
	SoGLColorIndexElement.cpp
	SoGLCoordinateElement.cpp
	SoGLDepthBufferElement.cpp
	SoGLDrawStyleElement.cpp
	SoGLEnvironmentElement.cpp
	SoGLLazyElement.cpp
	SoGLLightIdElement.cpp
	SoGLLinePatternElement.cpp
	SoGLLineWidthElement.cpp
	SoGLModelMatrixElement.cpp
	SoGLMultiTextureCoordinateElement.cpp
	SoGLMultiTextureEnabledElement.cpp
	SoGLMultiTextureMatrixElement.cpp
	SoGLMultiTextureImageElement.cpp
	SoGLNormalElement.cpp
	SoGLPointSizeElement.cpp
	SoGLPolygonOffsetElement.cpp
	SoGLProjectionMatrixElement.cpp
	SoGLRenderPassElement.cpp
	SoGLShapeHintsElement.cpp
	SoGLUpdateAreaElement.cpp
	SoGLVBOElement.cpp
	SoGLViewingMatrixElement.cpp
	SoGLViewportRegionElement.cpp
	SoGLDisplayList.cpp
	SoGLVertexAttributeElement.cpp
	SoResetMatrixElement.cpp
)

# build library
if(COIN_BUILD_SINGLE_LIB)
	if(NOT COIN_BUILD_LEGACY_GL_RENDERER)
		# Keep the legacy element ABI link-complete in a core-only single
		# library as well.  Use a separate object target so the compatibility
		# header selection is scoped to these legacy ABI sources only.
		add_library(glelements_compat OBJECT ${COIN_GLELEMENTS_FILES})
		target_compile_definitions(glelements_compat PRIVATE
			COIN_BUILD_LEGACY_GL_RENDERER=1
			COIN_GL_ELEMENTS_COMPATIBILITY=1)
		set(COIN_SINGLE_LIB_EXTRA_OBJECTS
			$<TARGET_OBJECTS:glelements_compat>
			CACHE INTERNAL "COIN_SINGLE_LIB_EXTRA_OBJECTS" FORCE)
	else()
		set(COIN_SINGLE_LIB_EXTRA_OBJECTS ""
			CACHE INTERNAL "COIN_SINGLE_LIB_EXTRA_OBJECTS" FORCE)
		foreach(SRCFILE ${COIN_GLELEMENTS_FILES})
			set(COIN_SOURCE ${COIN_SOURCE} elements/GL/${SRCFILE} CACHE INTERNAL "COIN_SOURCE")
		endforeach(SRCFILE)
	endif()
else()
	set(COIN_SINGLE_LIB_EXTRA_OBJECTS ""
		CACHE INTERNAL "COIN_SINGLE_LIB_EXTRA_OBJECTS" FORCE)
	add_library(glelements OBJECT ${COIN_GLELEMENTS_FILES})
	if(NOT COIN_BUILD_LEGACY_GL_RENDERER)
		# Keep the legacy element ABI link-complete, but compile it against
		# compatibility headers in a private object target. Core builds do
		# not register these elements for retained traversal and the legacy
		# pipeline remains rejected at its action boundary.
			target_compile_definitions(glelements PRIVATE
				COIN_BUILD_LEGACY_GL_RENDERER=1
				COIN_GL_ELEMENTS_COMPATIBILITY=1
			)
	endif()
endif()

# define documentation
foreach(SRCFILE ${COIN_GLELEMENTS_FILES})
	set(COIN_DOCUMENTATION_FILES ${COIN_DOCUMENTATION_FILES} ${PROJECT_SOURCE_DIR}/src/elements/GL/${SRCFILE} CACHE INTERNAL "COIN_DOCUMENTATION_FILES")
endforeach(SRCFILE)
set(COIN_DOCUMENTATION_FILES
	${COIN_DOCUMENTATION_FILES}
	${PROJECT_SOURCE_DIR}/src/elements/GL/SoResetMatrixElement.h
	CACHE INTERNAL "COIN_DOCUMENTATION_FILES"
)

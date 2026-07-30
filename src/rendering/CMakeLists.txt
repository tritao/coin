# source files
set(COIN_RENDERING_FILES
	SoGL.cpp
	SoGLBigImage.cpp
	SoGLDriverDatabase.cpp
	SoGLImage.cpp
	SoGLCubeMapImage.cpp
	SoGLNurbs.cpp
	SoRenderBackend.cpp
	SoRenderIR.cpp
	SoGLRenderBackend.cpp
	SoIDPickBuffer.cpp
	SoRenderManager.cpp
	SoRenderManagerP.cpp
	SoOffscreenRenderer.cpp
	SoOffscreenCGData.cpp
	SoOffscreenGLXData.cpp
	SoOffscreenWGLData.cpp
	SoVBO.cpp
	SoVertexLayout.cpp
	SoVAO.cpp
	SoVertexArrayIndexer.cpp
	CoinOffscreenGLCanvas.cpp
)

# Files excluded from public API documentation, included in complete documentation.
set(COIN_RENDERING_INTERNAL_FILES
	SoGL.h
	SoGL.cpp
	SoGLNurbs.h
	SoGLNurbs.cpp
	SoRenderIRP.h
	SoRenderBackend.h
	SoGLRenderBackend.h
	SoIDPickBuffer.h
	SoIDPickBuffer.cpp
	SoRenderManagerP.h
	SoRenderManagerP.cpp
	SoOffscreenCGData.h
	SoOffscreenCGData.cpp
	SoOffscreenGLXData.h
	SoOffscreenGLXData.cpp
	SoOffscreenWGLData.h
	SoOffscreenWGLData.cpp
	SoVBO.h
	SoVBO.cpp
	SoVertexLayout.h
	SoVertexLayout.cpp
	SoVAO.h
	SoVAO.cpp
	SoVertexArrayIndexer.h
	SoVertexArrayIndexer.cpp
	CoinOffscreenGLCanvas.h
	CoinOffscreenGLCanvas.cpp
)

# build library
if(COIN_BUILD_SINGLE_LIB)
	foreach(SRCFILE ${COIN_RENDERING_FILES})
		set(COIN_SOURCE ${COIN_SOURCE} rendering/${SRCFILE} CACHE INTERNAL "COIN_SOURCE")
	endforeach(SRCFILE)
else()
	add_library(rendering OBJECT ${COIN_RENDERING_FILES})
	# The retained renderer uses C++17 internally; do not raise the requirement
	# for Coin's installed public headers or unrelated targets.
	target_compile_features(rendering PRIVATE cxx_std_17)
	set_target_properties(rendering PROPERTIES CXX_EXTENSIONS OFF)
endif()

# define documentation
foreach(SRCFILE ${COIN_RENDERING_FILES})
	set(COIN_DOCUMENTATION_FILES ${COIN_DOCUMENTATION_FILES} ${PROJECT_SOURCE_DIR}/src/rendering/${SRCFILE} CACHE INTERNAL "COIN_DOCUMENTATION_FILES")
endforeach(SRCFILE)
foreach(SRCFILE ${COIN_RENDERING_INTERNAL_FILES})
	set(COIN_INTERNAL_DOCUMENTATION_FILES ${COIN_INTERNAL_DOCUMENTATION_FILES} ${PROJECT_SOURCE_DIR}/src/rendering/${SRCFILE} CACHE INTERNAL "COIN_INTERNAL_DOCUMENTATION_FILES")
endforeach(SRCFILE)

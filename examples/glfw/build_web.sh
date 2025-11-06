
emcc glfw.cpp -g -o bin/glfw.html -DCOIN_USE_EGL=1 \
    -s USE_WEBGL2=1 -s USE_GLFW=3 -s WASM=1 -sLEGACY_GL_EMULATION \
    ../../build_web/lib/libCoin.a

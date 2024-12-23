#!/usr/bin/env bash
set -ex
cd "$(dirname "$0")"

# check if debian and
# sudo apt install freeglut3-dev libglfw3-dev libglu-dev ninja-build libboost-dev

export COIN_PATH="$PWD/../.."
export COIN_BUILD_PATH="$COIN_PATH/${1:-bld}"
ninja -C $COIN_BUILD_PATH

mkdir -p bin/
g++ -O0 -ggdb glfw.cpp \
    -DCOIN_USE_GL_RENDERER=1 \
    -I$COIN_PATH/include -I$COIN_BUILD_PATH/include -L$COIN_BUILD_PATH/lib \
    -lCoin -lGL -lGLU -lglut -lglfw -lX11 \
    -o bin/glfw

### Uncomment for BGFX

# g++ -O0 -ggdb glfw.cpp \
#    -DBX_CONFIG_DEBUG=1 -DCOIN_USE_BGFX_RENDERER=1 \
#    -I$COIN_PATH/bgfx.cmake/bx/include -I$COIN_PATH/bgfx.cmake/bgfx/include \
#    -L$COIN_BUILD_PATH/bgfx.cmake/cmake/bgfx/ -lbgfx -lX11 \
#    -L$COIN_BUILD_PATH/bgfx.cmake/cmake/bimg/ -lbimg \
#    -L$COIN_BUILD_PATH/bgfx.cmake/cmake/bx/ -lbx \

###

export LD_LIBRARY_PATH=$COIN_BUILD_PATH/lib:"$LD_LIBRARY_PATH"
./bin/glfw
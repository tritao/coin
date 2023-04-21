# check if debian and
# sudo apt install freeglut3-dev libglfw3-dev libglu-dev ninja-build
# libboost-dev

set -e
export COIN_PATH=`pwd`/../..
export COIN_BUILD_PATH=$COIN_PATH/bld
ninja -C $COIN_BUILD_PATH

g++ -ggdb glfw.cpp \
    -I$COIN_PATH/include -I$COIN_BUILD_PATH/include -L$COIN_BUILD_PATH/lib \
    -lGL -lGLU -lglut -lglfw -lCoin -o glfw

g++ -ggdb glfw_bgfx.cpp \
    -DBX_CONFIG_DEBUG=1 \
    -I$COIN_PATH/bgfx.cmake/bx/include -I$COIN_PATH/bgfx.cmake/bgfx/include \
    -I$COIN_PATH/include -I$COIN_BUILD_PATH/include -L$COIN_BUILD_PATH/lib \
    -L$COIN_BUILD_PATH/bgfx.cmake/cmake/bgfx/ -lbgfx -lX11 \
    -L$COIN_BUILD_PATH/bgfx.cmake/cmake/bimg/ -lbimg \
    -L$COIN_BUILD_PATH/bgfx.cmake/cmake/bx/ -lbx \
    -lGL -lGLU -lglut -lglfw -lCoin \
    -o glfw_bgfx


#export LD_LIBRARY_PATH=/home/joao/dev/coin/bld/lib:"$LD_LIBRARY_PATH"

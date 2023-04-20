# check if debian and
# sudo apt install freeglut3-dev libglfw3-dev libglu-dev ninja-build
# libboost-dev

set -e
export COIN_PATH=`pwd`/../..
export COIN_BUILD_PATH=$COIN_PATH/bld
ninja -C $COIN_BUILD_PATH
g++ -ggdb glfw.cpp -I$COIN_PATH/include -I$COIN_BUILD_PATH/include -L$COIN_BUILD_PATH/lib -lGL -lGLU -lglut -lglfw -lCoin -o glfw
#export LD_LIBRARY_PATH=/home/joao/dev/coin/bld/lib:"$LD_LIBRARY_PATH"

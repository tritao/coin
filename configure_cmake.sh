rm -rf bld
mkdir -p bld && cd bld
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCOIN_BUILD_EGL=1 ..

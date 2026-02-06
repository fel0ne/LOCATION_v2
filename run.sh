#!/bin/bash
mkdir -p build
cd build
cmake ..
cmake --build . -j4
./my_app

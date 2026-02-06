@echo off
mkdir build 2>nul
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
cd Release
my_app.exe
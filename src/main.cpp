#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <iostream>
#include <thread>
#include <string>
#include <mutex>
#include <cmath>
#include <cpr/cpr.h>
#include <filesystem>
#include <fstream>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "nlohmann/json.hpp"

#include "zmq.hpp"

#include <pqxx/pqxx>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include "backend/structs.cpp"
#include "backend/consts.cpp"
#include "backend/variables.cpp"


#include "backend/map_alg.cpp"
#include "backend/net_fun.cpp"
#include "backend/run_host.cpp"
#include "backend/run_server.cpp"
#include "gui/rungui.cpp"




int main(int argc, char *argv[]) {
    runGui(); 
    return 0;
}
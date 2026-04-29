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
#include "stb_image.h"

#include "backend/structs.cpp"
#include "backend/consts.cpp"
#include "backend/variables.cpp"


#include "backend/map_alg.cpp"
#include "backend/net_fun.cpp"
#include "backend/run_host.cpp"
#include "backend/run_server.cpp"
#include "gui/rungui.cpp"



 //загруает png тайлы как текстуру opengl


int main(int argc, char *argv[]) {

    auto r = cpr::Get(cpr::Url{"https://api.github.com/repos/libcpr/cpr"});
    std::cout << r.status_code << std::endl;

    // Запускаем сервер в фоновом потоке
    // std::thread server_t(run_server); 
    // server_t.detach(); // Пусть живет сам по себе

    runGui(); // GUI запускаем строго в основном потоке



    return 0;
}
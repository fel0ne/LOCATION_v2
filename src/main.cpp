#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <iostream>
#include <thread>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "zmq.hpp"

#define LOG_FILE "data_log.json"

struct JSONData{
    float accuracy;
    float latitude;
    float longitude;
    char provider[64];
    int recordedTime;
    char source[64];
    long long timestamp;
};

void runGui() {
    // 1. Инициализация SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return;
    }

    // Установка атрибутов OpenGL перед созданием окна
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    // 2. Создание окна
    SDL_Window* window = SDL_CreateWindow("Aurora", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Window Error: " << SDL_GetError() << std::endl;
        return;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Включаем VSync (чтобы не было 1000 FPS и шума кулеров)

    // Инициализация GLEW
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW Error!" << std::endl;
        return;
    }

    // 3. Инициализация ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event); // Отдаем события ImGui
            if (event.type == SDL_EVENT_QUIT) running = false;
        }

        // Старт кадра ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // --- ТВОЙ ИНТЕРФЕЙС ТУТ ---
        ImGui::Begin("Aurora Dashboard");
        ImGui::Text("Hello from Linux!");
        if (ImGui::Button("Exit")) running = false;
        ImGui::End();
        // -------------------------

        // Рендеринг
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        SDL_GL_SwapWindow(window);
    }

    // Очистка
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}


void run_server(){
    zmq::context_t context(1); //количество потоков обрабатывающих все сокеты
    zmq::socket_t socket(context, zmq::socket_type::pull); //создае сокет
    socket.bind("tcp://*:4040");//привязываем сокет скорее порт т к *  говорит о том что в последствии будем слушать все адреса

    while(true){
        zmq::message_t message;
        socket.recv(&message);
        
        zmq_sleep(1000);
    }
}

int main(int argc, char *argv[]) {
    // Запускаем сервер в фоновом потоке
    // std::thread server_t(run_server); 
    // server_t.detach(); // Пусть живет сам по себе

    runGui(); // GUI запускаем строго в основном потоке

    return 0;
}
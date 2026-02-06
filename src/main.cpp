#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"  // ← Используем SDL3 бэкенд!
#include "imgui.h"
#include "implot.h"

void run_gui() {
    // 1) Инициализация SDL3
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return;
    }
    
    // 2) Создание окна в SDL3 (синтаксис изменился!)
    SDL_Window* window = SDL_CreateWindow(
        "Backend start - SDL3",
        1024, 768,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    
    if (!window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return;
    }
    
    // 3) Создание контекста OpenGL
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        std::cerr << "SDL_GL_CreateContext Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    
    SDL_GL_MakeCurrent(window, gl_context);
    
    // 4) Инициализация GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        SDL_GL_DestroyContext(gl_context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    
    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLEW version: " << glewGetString(GLEW_VERSION) << std::endl;

    // 5) Инициализация Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO(); 
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // Включаем поддержку viewport
    
    // 6) Установка стиля
    ImGui::StyleColorsDark();
    
    // Настройка стиля для viewport
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // 7) Инициализация бэкендов
    // ВАЖНО: Используем SDL3 бэкенд!
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 8) Основной цикл
    bool running = true;
    
    while (running) {
        // 8.1) Обработка событий SDL3
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Передаем события в ImGui SDL3 бэкенд
            ImGui_ImplSDL3_ProcessEvent(&event);
            
            // SDL3 использует SDL_EVENT_QUIT
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            
            // Закрытие окна
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
            
            // Escape для выхода
            if (event.type == SDL_EVENT_KEY_DOWN && 
                event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        // 8.2) Начинаем новый фрейм
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();  // ← SDL3 версия!
        ImGui::NewFrame();
        
        // 8.3) Docking пространство
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_None);

        // 8.4) Наш виджет с кнопкой
        {
            static int counter = 0;
            static bool show_demo = false;
            static bool show_implot_demo = false;
            
            ImGui::Begin("Hello, SDL3!", nullptr, ImGuiWindowFlags_NoCollapse);
            
            ImGui::Text("SDL3 + ImGui test with proper backend!");
            ImGui::Separator();
            
            // Кнопка - теперь должна работать!
            if (ImGui::Button("Click me!")) {
                counter++;
                std::cout << "Button clicked! Counter: " << counter << std::endl;
            }
            ImGui::SameLine();
            ImGui::Text("Counter: %d", counter);
            
            ImGui::Separator();
            
            // Другие элементы для теста
            ImGui::Checkbox("Show ImGui Demo", &show_demo);
            ImGui::SameLine();
            ImGui::Checkbox("Show ImPlot Demo", &show_implot_demo);
            
            // Информация
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                        1000.0f / io.Framerate, io.Framerate);
            ImGui::Text("Mouse: (%.1f, %.1f)", io.MousePos.x, io.MousePos.y);
            
            ImGui::Separator();
            
            if (ImGui::Button("Exit")) {
                running = false;
            }
            
            ImGui::End();
            
            // Демо окна
            if (show_demo)
                ImGui::ShowDemoWindow(&show_demo);
            if (show_implot_demo)
                ImPlot::ShowDemoWindow(&show_implot_demo);
        }

        // 8.5) Рендеринг
        ImGui::Render();
        
        int display_w, display_h;
        SDL_GetWindowSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        // Обновляем viewports если включены
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        
        SDL_GL_SwapWindow(window);
        
        // Небольшая задержка
        SDL_Delay(1);
    }

    // 9) Очистка
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();  // ← SDL3 версия!
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    
    SDL_GL_DestroyContext(gl_context);  // SDL3 использует DestroyContext вместо DeleteContext
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char *argv[]) {
    std::cout << "Starting application with SDL3 backend..." << std::endl;
    
    std::thread gui_thread(run_gui);
    gui_thread.join();

    // Здесь должен работать поток с сервером
    // std::thread zmq_thread(zmq_server_run); 
    // zmq_thread.join();
    
    std::cout << "Application finished." << std::endl;
    return 0;
}
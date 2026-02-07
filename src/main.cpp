#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"  //  Используем SDL3 бэкенд!
#include "imgui.h"
#include "implot.h"

void runGui(){
     // 1. Инициализация SDL3
    if (!SDL_Init(SDL_INIT_VIDEO)) { //return true if succes 
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
    }
    
    // 2. Создание окна
    SDL_Window* window = SDL_CreateWindow(
        "Aurora",  // Заголовок
        800,                     // Ширина
        600,                     // Высота
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_OPENGL //windows flags 
    );

    SDL_GLContext gl_context = SDL_GL_CreateContext(window); //это контекст типо кисть художника его юзает imgui
    
    if (!window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();

    }
    
    
    // 3. Основной цикл (обработка событий)
    bool running = true; //флаг для выхода
    while (running) { // основной цикл тут будет  сервер 
        SDL_Event event;
        while (SDL_PollEvent(&event)) {  //возвращает события из пула событий
            if (event.type == SDL_EVENT_QUIT) {// кнопка крестика
                running = false;
            }
            
            // Закрытие окна
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }
            
            // Нажатие клавиши Escape
            if (event.type == SDL_EVENT_KEY_DOWN && 
                event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }
        
        // Здесь будет рисование...
        
        // Небольшая задержка, чтобы не грузить CPU
        SDL_Delay(16); // ~60 FPS
    }
    
    // 4. Очистка
    SDL_DestroyWindow(window);
    SDL_Quit();
    
}

void run_server(){

}


// void run_host(){ // android --> server (not local process) --> this computer (host)
    
// }

int main(int argc, char *argv[]) {

    std::thread gui_t(runGui);
    gui_t.join();// join a gui thread





//-------------------------server thread------------------------------

    return 0;
}
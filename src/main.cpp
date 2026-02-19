#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <iostream>
#include <thread>
#include <string>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "nlohmann/json.hpp"

#include "zmq.hpp"

#include <pqxx/pqxx>


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


std::string make_grid_key(double lat, double lon) { // генерируем ключ чтобы в будущем менять значение в базе если оноже есть в неком радиусе
    std::stringstream ss;
    // Округляем до 4 знаков после запятой (шаг ~11 метров)
    ss << std::fixed << std::setprecision(5) << lat << ":" << lon;
    return ss.str();
}

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



//безшовность????
void run_server(){
    
    try {
        // 1. Подключаемся к дефолтной базе 'postgres'
        pqxx::connection temp_conn(" dbname=postgres user=fel0ne password=123");
        pqxx::nontransaction N(temp_conn);

        // 2. Проверяем, существует ли наша база
        pqxx::result r = N.exec_params(
            "SELECT 1 FROM pg_database WHERE datname = $1", 
            "location_db"
        );

        if (r.empty()) {
            std::cout << "Database 'location_db' not found. Creating..." << std::endl;
            // CREATE DATABASE нельзя запускать в транзакции, поэтому используем nontransaction
            N.exec("CREATE DATABASE location_db");
        }
        temp_conn.close(); // Закрываем временное соединение

        // 3. Теперь подключаемся уже к нашей базе и создаем таблицу
        pqxx::connection conn(" dbname=location_db user=fel0ne password=123");
        pqxx::work W(conn);
        W.exec(R"(CREATE TABLE IF NOT EXISTS location_history (
                id SERIAL PRIMARY KEY,
                latitude DOUBLE PRECISION NOT NULL,
                longitude DOUBLE PRECISION NOT NULL,
                accuracy REAL,               
                provider VARCHAR(20),          
                source VARCHAR(50),           
                recorded_time BIGINT NOT NULL, 
                event_timestamp BIGINT,       
                server_received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                grid_key TEXT UNIQUE
            );
        )");
        W.commit();
        std::cout << "Database and Table are ready!" << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Setup error: " << e.what() << std::endl;
    }



    zmq::context_t context(1); //количество потоков обрабатывающих все сокеты
    zmq::socket_t socket(context, zmq::socket_type::pull); //создае сокет
    socket.bind("tcp://*:4040");//привязываем сокет, скорее порт т.к. *  говорит о том что в последствии будем слушать все адреса
    pqxx::connection conn(" dbname=location_db user=fel0ne password=123");
    while(true){
        std::string message_str; 
        
        zmq::message_t message;
        socket.recv(&message);  //получение данных
        message_str = std::string(static_cast<char*>(message.data()), message.size()); //преобразуем в строку сообщениие, кастуем в чар чтобы избавится от указателя на пустоту
        
        //===================TEST=======================
        //message_str = R"({"accuracy":18.5,"latitude":55.04407833333333,"longitude":82.98355500000001,"provider":"gps","recordedTime":1763312279272,"source":"Кэш","timestamp":1763215517000})";
        //==============================================
        

        
        
        nlohmann::json json = nlohmann::json::parse(message_str);
       
        try {
            
            pqxx::work W(conn);
            std::string grid_key = make_grid_key(json["latitude"],json["longitude"]);

            W.exec_params(
                R"(INSERT INTO location_history 
                (latitude, longitude, accuracy, provider, source, recorded_time, event_timestamp, grid_key) 
                VALUES ($1, $2, $3, $4, $5, $6, $7, $8) 
                ON CONFLICT (grid_key) DO UPDATE SET 
                    latitude = EXCLUDED.latitude, 
                    longitude = EXCLUDED.longitude, 
                    accuracy = EXCLUDED.accuracy, 
                    recorded_time = EXCLUDED.recorded_time, 
                    event_timestamp = EXCLUDED.event_timestamp,
                    server_received_at = CURRENT_TIMESTAMP;)",
                json["latitude"].get<double>(),
                json["longitude"].get<double>(),
                json.value("accuracy", 0.0),
                json.value("provider", "unknown"),
                json.value("source", "unknown"),
                json["recordedTime"].get<long long>(),
                json.value("timestamp", 0LL),
                grid_key
            );

            // Если не вызвать W.commit(), данные НЕ сохранятся в базе!
            W.commit();

            std::cout << "[DB] Данные успешно сохранены!" << std::endl;

        } catch (const std::exception &e) {
            std::cerr << "[DB Error] Ошибка вставки: " << e.what() << std::endl;
            // Транзакция W автоматически откатится (rollback) при выходе из области видимости
        }

        //std::cerr<<json["latitude"];

        //zmq_sleep(1000);//спим чтобы не грузить поток
        

    
    
    }
}

int main(int argc, char *argv[]) {

    

    // Запускаем сервер в фоновом потоке
    std::thread server_t(run_server); 
    server_t.detach(); // Пусть живет сам по себе

    runGui(); // GUI запускаем строго в основном потоке



    return 0;
}
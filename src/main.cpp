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
#include "stb_image.h" //загруает png тайлы как текстуру opengl





#define LOG_FILE "data_log.json"

std::string connect_IP = ""; 

bool is_host_running = false;

struct JSONData{
    float accuracy;
    double latitude;
    double longitude;
    std::string provider;
    long long recordedTime;
    std::string source;
    long long timestamp;

    void clear() {
        latitude = 0; longitude = 0; accuracy = 0;
        provider = ""; source = "";
        recordedTime = 0; timestamp = 0;
    }
};


std::string make_grid_key(double lat, double lon) { // генерируем ключ чтобы в будущем менять значение в базе если оно же есть в неком радиусе
    std::stringstream ss;
    // Округляем до 4-5 знаков после запятой (шаг ~11 метров)
    ss << std::fixed << std::setprecision(4) << lat << ":" << lon; //хи хи питону привет
    return ss.str();
}


std::vector<double> plot_lats;
std::vector<double> plot_lons;
std::vector<double> plot_rsrp; 
std::vector<double> gps_lats, gps_lons;
std::vector<double> net_lats, net_lons, net_rsrp;
std::mutex data_mutex;

void refresh_plot_data() {
    try {
        pqxx::connection conn("dbname=location_db user=fel0ne password=123");
        pqxx::read_transaction R(conn);
        
        auto rows = R.exec(R"(
            SELECT lh.latitude, lh.longitude, lh.provider, cd.signal_strength 
            FROM location_history lh
            LEFT JOIN cellular_data cd ON lh.id = cd.location_id
            ORDER BY lh.id ASC
        )");

        std::lock_guard<std::mutex> lock(data_mutex);
        gps_lats.clear(); gps_lons.clear();
        net_lats.clear(); net_lons.clear(); net_rsrp.clear();

        for (auto row : rows) {
            std::string provider = row[2].as<std::string>();
            double lat = row[0].as<double>();
            double lon = row[1].as<double>();

            if (provider == "gps") {
                gps_lats.push_back(lat);
                gps_lons.push_back(lon);
            } else {
                net_lats.push_back(lat);
                net_lons.push_back(lon);
                net_rsrp.push_back(row[3].is_null() ? -120.0 : row[3].as<double>());
            }
        }
    } catch (...) {}
}

// долгота в X тайла
double lon2tile(double lon, int zoom) {
    return (lon + 180.0) / 360.0 * std::pow(2.0, zoom);
}

// широта в Y тайла
double lat2tile(double lat, int zoom) {
    return (1.0 - std::log(std::tan(lat * M_PI / 180.0) + 1.0 / std::cos(lat * M_PI / 180.0)) / M_PI) / 2.0 * std::pow(2.0, zoom);
}


double tilex2lon(int x, int z) {
    return x / std::pow(2.0, z) * 360.0 - 180.0;
}

double tiley2lat(int y, int z) {
    double n = M_PI - 2.0 * M_PI * y / std::pow(2.0, z);
    return 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

void download_tile_cpr(int z, int x, int y) {
    int max_tile = std::pow(2, z) - 1;
    if (x < 0 || x > max_tile || y < 0 || y > max_tile) return;
    std::string folder = "tiles/" + std::to_string(z) + "/" + std::to_string(x);
    std::string path = folder + "/" + std::to_string(y) + ".png";

    // Если файл уже есть, не качаем
    if (std::filesystem::exists(path)) return;

    // Создаем папки, если их нет
    std::filesystem::create_directories(folder);

    // Запрос к OpenStreetMap
    cpr::Response r = cpr::Get(
        cpr::Url{"https://tile.openstreetmap.org/" + std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png"},
        cpr::Header{{"User-Agent", "AuroraMapApp/1.0 (your@email.com)"}}, // OSM требует User-Agent
        cpr::VerifySsl{false}
    );

    if (r.status_code == 200) {
        std::ofstream ofs(path, std::ios::binary);
        ofs << r.text;
        ofs.close();
        std::cout << "[MAP] Downloaded: " << path << std::endl;
    } else {
        std::cerr << "[MAP] Error " << r.status_code << " for tile " << x << "," << y << std::endl;
    }
}

void thread_loader(int z, int x, int y) {
    download_tile_cpr(z, x, y);
}

GLuint LoadTexture(const char* filename) { // получаем путь к файлу 
    int width, height, channels; 
    stbi_set_flip_vertically_on_load(false); //говорим не переворачивать картинку
    
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4); //даем файлик на растерзания  аолучая сырые байты цвета 
    //при этом достаем 4 канала принудительно чтобы видеокарта не сильно напрягалась
    if (!data) {
        std::cerr << "[GL ERROR] stb_load failed for: " << filename << std::endl;
        return 0;
    }

    GLuint textureID;
    glGenTextures(1, &textureID); //выделяем айди
    glBindTexture(GL_TEXTURE_2D, textureID); //биндим этот айди чтобы все изменения ниже применялись именно к нему
    //фильтрация боремся с лесинками
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //боремся с неприятными швами
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    //копируем данные в видеокарту  из оперативной памяти
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    //соответственно вычещаем уже не нужные данные из оперативки
    stbi_image_free(data);

    std::cout << "[GL] Loaded texture: " << filename << " ID: " << textureID << std::endl;
    return textureID;
}

// int calculate_zoom(ImPlotRect limits) {
//     double width = std::abs(limits.X.Max - limits.X.Min);
//     if (width <= 0) return 15;
//     // Логарифмическая зависимость зума от ширины экрана в градусах
//     int z = (int)std::floor(std::log2(360.0 / width));
//     if (z < 1) z = 1;
//     if (z > 18) z = 18;
//     return z;
// }




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
        W.exec(R"(
            CREATE TABLE IF NOT EXISTS location_history (
                id SERIAL PRIMARY KEY,
                latitude DOUBLE PRECISION NOT NULL,
                longitude DOUBLE PRECISION NOT NULL,
                accuracy REAL,
                recorded_time BIGINT NOT NULL,
                grid_key TEXT UNIQUE
            );
            CREATE TABLE IF NOT EXISTS cellular_data (
                id SERIAL PRIMARY KEY,
                location_id INTEGER REFERENCES location_history(id) ON DELETE CASCADE,
                network_type VARCHAR(10), -- LTE, GSM, NR
                cell_id BIGINT,           -- CI или NCI
                mcc VARCHAR(5),
                mnc VARCHAR(5),
                signal_strength REAL      -- RSRP или dbm
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
        

        
        
        auto json = nlohmann::json::parse(message_str);

        try {
            pqxx::work W(conn);
            std::string grid_key = make_grid_key(json["latitude"], json["longitude"]);

            // 1. Сохраняем локацию и получаем ID вставленной строки
            pqxx::result res = W.exec_params(
                R"(INSERT INTO location_history 
                (latitude, longitude, accuracy, recorded_time, grid_key) 
                VALUES ($1, $2, $3, $4, $5) 
                ON CONFLICT (grid_key) DO UPDATE SET recorded_time = EXCLUDED.recorded_time
                RETURNING id;)",
                json["latitude"].get<double>(),
                json["longitude"].get<double>(),
                json.value("accuracy", 0.0f),
                json["recordedTime"].get<long long>(),
                grid_key
            );

            int location_id = res[0][0].as<int>();

            // 2. Если есть данные о сотах — парсим их
            if (json.contains("cell_info") && json["cell_info"].is_array()) {
                for (auto& cell : json["cell_info"]) {
                    std::string type = cell.value("type", "unknown");
                    long long cid = 0;
                    float signal = 0.0f;

                    // Извлекаем специфичные для типа данные
                    if (cell.contains("id")) {
                        auto id_obj = cell["id"];
                        if (type == "LTE") cid = id_obj.value("ci", 0L);
                        else if (type == "NR") cid = id_obj.value("nci", 0L);
                        else if (type == "GSM") cid = id_obj.value("cid", 0L);
                    }

                    if (cell.contains("signal")) {
                        auto sig_obj = cell["signal"];
                        if (type == "LTE") signal = sig_obj.value("rsrp", 0.0f);
                        else if (type == "GSM") signal = sig_obj.value("dbm", 0.0f);
                        else if (type == "NR") signal = sig_obj.value("ss_rsrp", 0.0f);
                    }

                    W.exec_params(
                        "INSERT INTO cellular_data (location_id, network_type, cell_id, signal_strength) VALUES ($1, $2, $3, $4)",
                        location_id, type, cid, signal
                    );
                }
            }
            W.commit();
        } catch (const std::exception &e) {
            std::cerr << "[DB Error] " << e.what() << std::endl;
        }

        //std::cerr<<json["latitude"];

        //zmq_sleep(1000);//спим чтобы не грузить поток
        

    
    
    }
}




void run_host() {
    try {
        // 1. Подключаемся к дефолтной базе 'postgres' для проверки существования location_db
        pqxx::connection temp_conn("dbname=postgres user=fel0ne password=123");
        pqxx::nontransaction N(temp_conn);

        pqxx::result r = N.exec_params("SELECT 1 FROM pg_database WHERE datname = $1", "location_db");

        if (r.empty()) {
            std::cout << "Database 'location_db' not found. Creating..." << std::endl;
            N.exec("CREATE DATABASE location_db");
        }
        temp_conn.close();

        // 2. Инициализация таблиц
        pqxx::connection conn_init("dbname=location_db user=fel0ne password=123");
        pqxx::work W_init(conn_init);
        W_init.exec(R"(
            CREATE TABLE IF NOT EXISTS location_history (
                id SERIAL PRIMARY KEY,
                latitude DOUBLE PRECISION NOT NULL,
                longitude DOUBLE PRECISION NOT NULL,
                accuracy REAL,               
                provider VARCHAR(20),          
                source VARCHAR(50),           
                recorded_time BIGINT NOT NULL, 
                event_timestamp BIGINT,       
                server_received_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                grid_key TEXT UNIQUE);
            CREATE TABLE IF NOT EXISTS cellular_data (
                id SERIAL PRIMARY KEY,
                location_id INTEGER REFERENCES location_history(id) ON DELETE CASCADE,
                network_type VARCHAR(10),
                cell_id BIGINT,
                mcc VARCHAR(5),
                mnc VARCHAR(5),
                signal_strength REAL,
                extra_data JSONB,
                UNIQUE(location_id, network_type)
            );
        )");
        W_init.commit();
        std::cout << "Database and Table are ready!" << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Setup error: " << e.what() << std::endl;
        return; // Если база не настроилась, дальше идти нет смысла
    }

    // 3. Настройка ZMQ
    std::string full_address = "tcp://" + connect_IP + ":5556";
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::pull);
    
    try {
        socket.connect(full_address);
    } catch (const zmq::error_t& e) {
        std::cerr << "ZMQ Connect error: " << e.what() << std::endl;
        return;
    }

    pqxx::connection conn("dbname=location_db user=fel0ne password=123");

    // 4. Основной цикл приема данных
    while(true) {
        try {
            zmq::message_t message;
            auto res = socket.recv(message, zmq::recv_flags::none);
            if (message.size() == 0) continue;

            std::string message_str = std::string(static_cast<char*>(message.data()), message.size());
            std::cerr << "Data received: " << message_str << std::endl;

            auto json = nlohmann::json::parse(message_str);
            
            // Начинаем транзакцию для записи
            pqxx::work W(conn);
            std::string grid_key = make_grid_key(json["latitude"], json["longitude"]);

            pqxx::result res_db = W.exec_params(
                R"(INSERT INTO location_history 
                (latitude, longitude, accuracy, provider, source, recorded_time, event_timestamp, grid_key) 
                VALUES ($1, $2, $3, $4, $5, $6, $7, $8) 
                ON CONFLICT (grid_key) DO UPDATE SET 
                    accuracy = EXCLUDED.accuracy,
                    recorded_time = EXCLUDED.recorded_time,
                    event_timestamp = EXCLUDED.event_timestamp,
                    provider = EXCLUDED.provider,
                    source = EXCLUDED.source
                RETURNING id;)",
                json["latitude"].get<double>(),
                json["longitude"].get<double>(),
                json.value("accuracy", 0.0),
                json.value("provider", "unknown"),
                json.value("source", "unknown"),
                json["recordedTime"].get<long long>(),
                json.value("timestamp", 0LL),
                grid_key
            );

            int location_id = res_db[0][0].as<int>();

            if (json.contains("telephony") && json["telephony"].is_object()) {
                auto tel = json["telephony"];
                for (auto& [type, content] : tel.items()) {
                    long long cid = 0;
                    float signal = 0.0f;
                    std::string mcc = "0", mnc = "0";
                    nlohmann::json extra = content;

                    if (content.contains("identity")) {
                        auto id_obj = content["identity"];
                        mcc = id_obj.value("mcc", "0");
                        mnc = id_obj.value("mnc", "0");
                        if (type == "LTE") cid = id_obj.value("ci", 0LL);
                        else if (type == "NR_5G") cid = id_obj.value("nci", 0LL);
                        else if (type == "GSM") cid = id_obj.value("cid", 0LL);
                    }

                    if (content.contains("signal")) {
                        auto sig_obj = content["signal"];
                        if (type == "LTE") signal = sig_obj.value("rsrp", 0.0f);
                        else if (type == "GSM") signal = sig_obj.value("dbm", 0.0f);
                        else if (type == "NR_5G") signal = sig_obj.value("ss_rsrp", 0.0f);
                    }

                    W.exec_params(
                        R"(INSERT INTO cellular_data (location_id, network_type, cell_id, mcc, mnc, signal_strength, extra_data) 
                        VALUES ($1, $2, $3, $4, $5, $6, $7)
                        ON CONFLICT (location_id, network_type) DO UPDATE SET 
                        extra_data = EXCLUDED.extra_data,
                        signal_strength = EXCLUDED.signal_strength;)",
                        location_id, type, cid, mcc, mnc, signal, extra.dump() 
                    );
                }
            }

            W.commit(); // Завершаем транзакцию
            std::cout << "[SERVER] Data saved, refreshing GUI..." << std::endl;
            
            // Обновляем данные для графиков
            refresh_plot_data();

        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "[JSON ERROR] " << e.what() << std::endl;
        } catch (const zmq::error_t& e) {
            std::cerr << "[ZMQ ERROR] " << e.what() << std::endl;
            // Если ошибка серьезная (например, прервано соединение), можно выйти или попробовать переподключиться
        } catch (const std::exception& e) {
            std::cerr << "[RUNTIME ERROR] " << e.what() << std::endl;
        }
    }
}


void runGui() {
    // 1. Инициализация SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    SDL_Window* window = SDL_CreateWindow("Aurora", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Window Error: " << SDL_GetError() << std::endl;
        return;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); 

    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW Error!" << std::endl;
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool running = true;
    bool mode = true; 
    bool radius_btn = false;
    bool gps_line_btn = false;
    bool network_line_btn = true;
    static std::map<std::string, GLuint> tile_cache; // база данных типо которая построена нв контейнере ключ-значение придется чистить переодически все тайлы в видеокарту не влезут

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();


        

        // --- ЛЕВАЯ ПАНЕЛЬ ---
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.2f, io.DisplaySize.y));
        ImGui::Begin("Control Panel", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        {

            static char ip_buffer[64] = "123.123.123.123"; 

        ImGui::Text("VPS Configuration");
        // Поле ввода: берет данные из ip_buffer
        ImGui::InputText("Server IP", ip_buffer, IM_ARRAYSIZE(ip_buffer));


        if (ImGui::Button("Connect to VPS", ImVec2(-1, 30))) {
            if (!is_host_running) {
                connect_IP = std::string(ip_buffer);
                is_host_running = true; // Ставим флаг
                std::thread host_t(run_host);
                host_t.detach();
            }
        }
        ImGui::Separator();

            ImGui::Text("Server Status: "); 
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "ONLINE");

            ImGui::Separator();

            if (ImGui::Button("Update Data", ImVec2(-1, 30))) { 
                std::thread(refresh_plot_data).detach();
            }

            if (ImGui::Button("Change mode", ImVec2(-1, 30))) {
                mode = !mode;
            }


            if (ImGui::Button("View circles", ImVec2(-1, 30))) {
                radius_btn = !radius_btn;
            }

            if (ImGui::Button("View gps line", ImVec2(-1, 30))) {
                gps_line_btn = !gps_line_btn;
            }

            if (ImGui::Button("View network line", ImVec2(-1, 30))) {
                network_line_btn = !network_line_btn;
            }

            
            ImGui::Separator();
            ImGui::Text("Last Log:");
            ImGui::BeginChild("LogRegion", ImVec2(0, 200), true);
            if (mode) {
                ImGui::Text("Starting server... ");
                ImGui::Text("Connection successful ");
            } else {
                ImGui::Text("Connection to host... ");
            }
            ImGui::EndChild();
        } 
        ImGui::End();

        // --- ПРАВАЯ ПАНЕЛЬ (КАРТА) ---
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.2f, 0));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y));
        ImGui::Begin("Map View", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        {

        if (ImGui::BeginTabBar("Tabs")){  
            if (ImGui::BeginTabItem("MAP")){ 
        
                 
            if (ImPlot::BeginPlot("##MainMap", ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
                
                // 1. НАСТРОЙКА ОСЕЙ (Сначала это!)
                ImPlot::SetupAxes("Longitude", "Latitude");
                ImPlot::SetupAxesLimits(82.8, 83.1, 54.9, 55.1, ImGuiCond_FirstUseEver);
                
                // 2. ПОЛУЧАЕМ ТЕКУЩИЕ ГРАНИЦЫ И ЗУМ
                ImPlotRect limits = ImPlot::GetPlotLimits();
                
                // Считаем зум динамически
                int zoom = 15; // по умолчанию
                double width = std::abs(limits.X.Max - limits.X.Min);
                if (width > 0) {
                    zoom = (int)std::floor(std::log2(360.0 / width*2)); //360 - так как ширина делится на 360 градусов делим на текушую ширину окна и преминеям логарифм и получим то сколько тайлов надо взять
                    if (zoom < 1) zoom = 1;
                    if (zoom > 18) zoom = 18;
                }



                // Лимит текстур (500 штук ~ 130-150 Мб видеопамяти)
                if (tile_cache.size() > 500) {
                    std::cout << "[MEM] Cleaning Up GPU memory..." << std::endl;
                    for (auto const& [path, texID] : tile_cache) {
                        if (texID > 0) {
                            GLuint id = texID;
                            glDeleteTextures(1, &id); // Освобождаем память в видеокарте
                        }
                    }
                    tile_cache.clear(); // Чистим сам список
                }

                // 3. РАСЧЕТ ТАЙЛОВ
                if (limits.X.Min > -180 && limits.X.Max < 180) { //проверка от дурака чтоб ничего не падало если улетим за границы карты
                    
                    //считаем диапозоны для видимых в данный момент тайлов
                    int x_start = (int)std::floor(lon2tile(limits.X.Min, zoom));
                    int x_end   = (int)std::floor(lon2tile(limits.X.Max, zoom));
                    int y_start = (int)std::floor(lat2tile(limits.Y.Max, zoom)); 
                    int y_end   = (int)std::floor(lat2tile(limits.Y.Min, zoom));

                    // Рисуем, если область не слишком огромная (защита от лагов)
                    if (std::abs(x_end - x_start) < 20 && std::abs(y_end - y_start) < 20) { //если количесто тайлов в камере огромное то скипаем отрисовку (число 20 на вскидку выбрано)
                        for (int x = x_start; x <= x_end; ++x) {
                            for (int y = y_start; y <= y_end; ++y) {
                                std::string path = "tiles/" + std::to_string(zoom) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png";// путь для запроса

                                GLuint texID = 0; //айди картинки в видеопамяти для отрисовки (0 - пусто) в моем случае еще промежуточное состояние скачки

                                // ПРОВЕРКА КЭША И ДИСКА
                                if (tile_cache.count(path)) {
                                    texID = tile_cache[path];
                                    
                                    // Если в кэше 0 (значит раньше файла не было), проверим - вдруг докачался?
                                    if (texID == 0 && std::filesystem::exists(path)) {
                                        texID = LoadTexture(path.c_str());
                                        tile_cache[path] = texID;
                                    }
                                } 
                                else {
                                    // Если вообще не знаем про такой тайл
                                    if (std::filesystem::exists(path)) {
                                        texID = LoadTexture(path.c_str());
                                        tile_cache[path] = texID;
                                    } else {
                                        // Файла нет - ставим метку 0 и запускаем поток на скачку
                                        tile_cache[path] = 0;
                                        std::thread(thread_loader, zoom, x, y).detach(); ///нам не нужно ожидать его завершения это будет тормозить основной поток пусть работает в фоне и экранируем на всякий 
                                    }
                                }

                                // ОТРИСОВКА ТАЙЛА
                                if (texID > 0) {
                                    double l_lon = tilex2lon(x, zoom);
                                    double r_lon = tilex2lon(x + 1, zoom);
                                    double t_lat = tiley2lat(y, zoom);
                                    double b_lat = tiley2lat(y + 1, zoom);
                                    
                                    // ImPlotPoint(X_min, Y_min), ImPlotPoint(X_max, Y_max)
                                    ImPlot::PlotImage(path.c_str(), (void*)(intptr_t)texID, 
                                                    ImPlotPoint(l_lon, b_lat), 
                                                    ImPlotPoint(r_lon, t_lat));
                                }
                            }
                        }
                    }
                }

                // 4. ТРЕК (Красная линия поверх карты)
                std::lock_guard<std::mutex> lock(data_mutex);

                // 1. Рисуем синюю линию (GPS)
                if (!gps_lats.empty() && gps_line_btn == true) {
                    ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.4f, 1.0f, 1.0f), 2.0f); // Синий
                    ImPlot::PlotLine("GPS Path", gps_lons.data(), gps_lats.data(), (int)gps_lats.size());
                }

                // 2. Рисуем красную линию (Network) и круги покрытия
                if (!net_lats.empty() && network_line_btn == true) {
                    ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), 2.0f); // Красный
                    ImPlot::PlotLine("Network Path", net_lons.data(), net_lats.data(), (int)net_lats.size());
                    if(radius_btn == true){
                    // Рисуем круги для каждой точки Network
                        for (size_t i = 0; i < net_lats.size(); ++i) {
                            // Рассчитываем радиус в градусах координат (очень грубо: 0.001 ~ 111 метров)
                            // Чем лучше сигнал (например -70), тем меньше радиус. Чем хуже (-110), тем больше.
                            double rsrp = net_rsrp[i];
                            double radius = (std::abs(rsrp) - 40.0) * 0.00005; // Коэффициент подбери под масштаб

                            if (radius > 0) {
                                // Преобразуем координаты GPS в пиксели на экране внутри графика
                                ImVec2 pos = ImPlot::PlotToPixels(ImPlotPoint(net_lons[i], net_lats[i]));
                                // Преобразуем радиус из координат в пиксели (грубо)
                                float r_pixels = ImPlot::PlotToPixels(ImPlotPoint(net_lons[i] + radius, net_lats[i])).x - pos.x;

                                // Рисуем круг в DrawList
                                ImPlot::GetPlotDrawList()->AddCircleFilled(pos, std::abs(r_pixels), IM_COL32(255, 0, 0, 50));
                                ImPlot::GetPlotDrawList()->AddCircle(pos, std::abs(r_pixels), IM_COL32(255, 0, 0, 150));
                            }
                        }
                    }
                }
                ImPlot::EndPlot();
            }
        
        ImGui::EndTabItem() ;
            }
            if (ImGui::BeginTabItem("GRAPHS")){ 
               std::lock_guard<std::mutex> lock(data_mutex);
                
                
                if (ImGui::BeginTable("GraphsTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInner)) {
                    ImGui::TableNextRow();
                    
                    // 1. Линейный график RSRP
                    ImGui::TableSetColumnIndex(0);
                    if (ImPlot::BeginPlot("LTE RSRP History", ImVec2(-1, 300))) {
                        ImPlot::SetupAxes("Index", "dBm");
                        ImPlot::SetupAxesLimits(0, 1000, -140, -40, ImGuiCond_FirstUseEver);
                        if (!net_rsrp.empty()) {
                            ImPlot::SetNextFillStyle(ImVec4(0, 1, 0, 1), 0.25f);
                            // Рисуем закрашенную область
                            ImPlot::PlotShaded("RSRP Area", net_rsrp.data(), (int)net_rsrp.size(), -140);
                            ImPlot::PlotLine("RSRP", net_rsrp.data(), (int)net_rsrp.size());
                        }
                        ImPlot::EndPlot();
                    }

                    // 2. Полярный график (Ручной пересчет)
                    ImGui::TableSetColumnIndex(1);
                    if (ImPlot::BeginPlot("Signal Azimuth", ImVec2(-1, 300), ImPlotFlags_Equal)) {
                        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickLabels);
                        
                        static float polar_x[360], polar_y[360];
                        for (int i = 0; i < 360; ++i) { 
                            float angle = i * (float)M_PI / 180.0f;
                            float mag = 0.5f + 0.5f * sinf(i * 0.05f); 
                            polar_x[i] = mag * cosf(angle); // X = R * cos(A)
                            polar_y[i] = mag * sinf(angle); // Y = R * sin(A)
                        }
                        ImPlot::PlotLine("Directional Gain", polar_x, polar_y, 360);
                        ImPlot::EndPlot();
                    }

                    ImGui::TableNextRow();

                    // 3. Тепловая карта (Heatmap)
                    ImGui::TableSetColumnIndex(0);
                    if (ImPlot::BeginPlot("Signal Density Heatmap", ImVec2(-1, 300))) {
                        static float heatmap_data[400]; 
                        for (int i = 0; i < 400; ++i) heatmap_data[i] = (float)(i % 20) / 20.0f;
                        ImPlot::PlotHeatmap("Cell Density", heatmap_data, 20, 20, 0, 1);
                        ImPlot::EndPlot();
                    }

                    // 4. SNR (Простая синусоида для примера 3D-поверхности)
                    ImGui::TableSetColumnIndex(1);
                    if (ImPlot::BeginPlot("Quality Metrics", ImVec2(-1, 300))) {
                        static float bars_data[10] = {1, 2, 4, 8, 16, 8, 4, 2, 1, 5};
                        ImPlot::PlotBars("SNR Levels", bars_data, 10);
                        ImPlot::EndPlot();
                    }

                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar() ;
        }
        ImGui::End();
        }

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

int main(int argc, char *argv[]) {

    auto r = cpr::Get(cpr::Url{"https://api.github.com/repos/libcpr/cpr"});
    std::cout << r.status_code << std::endl;

    // Запускаем сервер в фоновом потоке
    // std::thread server_t(run_server); 
    // server_t.detach(); // Пусть живет сам по себе

    runGui(); // GUI запускаем строго в основном потоке



    return 0;
}
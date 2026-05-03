void runGui() {
   auto calculate_hata_radius = [&](double rsrp, double freq, double hb) -> double {
        if (freq <= 0) return 0.0001;
        
        // Ограничиваем hb снизу, чтобы логарифм не сходил с ума
        double safe_hb = std::max(hb, 3.0); 
        double hm = 1.5;
        
        // Коэффициент для частот LTE (модель COST231-Hata)
        double Cm = 3.0; // Константа для плотной городской застройки
        
        // Базовая формула затухания (Path Loss) в дБ
        // L = 46.3 + 33.9*log10(f) - 13.82*log10(hb) - Ch + (44.9 - 6.55*log10(hb))*log10(d)
        
        double Ch = 0.8 + (1.1 * log10(freq) - 0.7) * hm - 1.56 * log10(freq);
        double a = 46.3 + 33.9 * log10(freq) - 13.82 * log10(safe_hb) - Ch + Cm;
        double b = 44.9 - 6.55 * log10(safe_hb);
        
        // Мощность передатчика БС (возьмем средние 43 дБм)
        double tx_power = 43.0; 
        double path_loss = tx_power - rsrp;
        
        double radius_km = pow(10, (path_loss - a) / b);
        return radius_km;
    };

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
    static float hb_slider = 30.0f;
    bool mode = true; 
    bool radius_btn = false;
    bool gps_line_btn = false;
    bool network_line_btn = true;
    bool heat_btn = false;
    static std::map<std::string, GLuint> tile_cache; // база данных типо которая построена нв контейнере ключ-значение придется чистить переодически все тайлы в видеокарту не влезут
    static std::map<std::string, GLuint> heatTile_cache;

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
        ImGui::Text("File Import (.jsonl)");
        static char import_path[256] = "data_log.json"; // Путь по умолчанию
        ImGui::InputText("File Path", import_path, IM_ARRAYSIZE(import_path));

        if (ImGui::Button("Import from File", ImVec2(-1, 30))) {
            // Запускаем импорт в отдельном потоке, чтобы GUI не фризило
            std::string path_str = import_path;
            std::thread([path_str]() {
                import_from_jsonl(path_str);
            }).detach();
        }


        ImGui::Separator();

            ImGui::Text("Server Status: "); 
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "ONLINE");

            ImGui::Separator();

            if (ImGui::Button("Update Data", ImVec2(-1, 30))) { 
                std::thread(refresh_plot_data).detach();
                if (heatTile_cache.size() > 500) {
                    std::cout << "[MEM] Cleaning Up GPU memory..." << std::endl;

                    for (auto const& [heat_path, heat_texID] : heatTile_cache) {
                        if (heat_texID > 0) {
                            GLuint id = heat_texID;
                            glDeleteTextures(1, &id); // Освобождаем память в видеокарте
                        }
                    }
                    heatTile_cache.clear();
                }
            }
            
            ImGui::SliderFloat("BS Height (hb)", &hb_slider, 10.0f, 100.0f, "%.1f m");

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
            if (ImGui::Button("View heat map", ImVec2(-1, 30))) {
                heat_btn = !heat_btn;
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


                if (heatTile_cache.size() > 500) {
                    std::cout << "[MEM] Cleaning Up GPU memory..." << std::endl;

                    for (auto const& [heat_path, heat_texID] : heatTile_cache) {
                        if (heat_texID > 0) {
                            GLuint id = heat_texID;
                            glDeleteTextures(1, &id); // Освобождаем память в видеокарте
                        }
                    }
                    heatTile_cache.clear();
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
                                std::string heat_path = "heatTiles/" + std::to_string(zoom) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png";// путь для запроса
                                GLuint texID = 0; //айди картинки в видеопамяти для отрисовки (0 - пусто) в моем случае еще промежуточное состояние скачки
                                GLuint heat_texID = 0;
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

                                if(heatTile_cache.count(heat_path)){
                                    heat_texID = heatTile_cache[heat_path];
                                    if(heat_texID == 0 && std::filesystem::exists(heat_path)){
                                        heat_texID = LoadTexture(heat_path.c_str());
                                        heatTile_cache[heat_path] = heat_texID;
                                    }    
                                }
                                else{
                                    if (std::filesystem::exists(heat_path)) {
                                        heat_texID = LoadTexture(heat_path.c_str());
                                        heatTile_cache[heat_path] = heat_texID;
                                    } else {
                                        // Файла нет - ставим метку 0 и запускаем поток на скачку
                                        heatTile_cache[heat_path] = 0;
                                        std::thread(generate_heat_map_tile, zoom, x, y).detach(); ///нам не нужно ожидать его завершения это будет тормозить основной поток пусть работает в фоне и экранируем на всякий 
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
                                    if(heat_texID > 0 && heat_btn == true){
                                        ImPlot::PlotImage(heat_path.c_str(), (void*)(intptr_t)heat_texID, 
                                                    ImPlotPoint(l_lon, b_lat), 
                                                    ImPlotPoint(r_lon, t_lat));

                                    }
                                }
                            }
                        }
                    }
                }

                // 4. ТРЕК (Красная линия поверх карты)
                std::lock_guard<std::mutex> lock(data_mutex);

                // 1. Рисуем синюю линию (GPS) сегментами
                if (!gps_lats.empty() && gps_line_btn == true) {
                    size_t start_idx = 0;
                    int color_offset = 0; // Для смены оттенков разных прогулок

                    for (size_t i = 1; i < gps_lats.size(); ++i) {
                        // Проверяем разницу во времени (в миллисекундах)
                        // Предполагаем, что у тебя есть массив gps_times, синхронный с gps_lats
                        if (std::abs(gps_times[i] - gps_times[i - 1]) > 3600000) { 
                            
                            // Рисуем накопившийся сегмент до разрыва
                            std::string label = "GPS Segment " + std::to_string(color_offset);
                            ImVec4 col = ImPlot::GetColormapColor((10 + color_offset) % ImPlot::GetColormapSize()); // Начинаем с синих оттенков
                            ImPlot::SetNextLineStyle(col, 5.0f);
                            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f);
                            ImPlot::PlotLine(label.c_str(), &gps_lons[start_idx], &gps_lats[start_idx], (int)(i - start_idx));
                            
                            start_idx = i;
                            color_offset++;
                        }
                    }
                    // Рисуем последний (или единственный) сегмент
                    ImPlot::SetNextLineStyle(ImPlot::GetColormapColor((10 + color_offset) % ImPlot::GetColormapSize()), 5.0f);
                    ImPlot::PlotLine("GPS Active", &gps_lons[start_idx], &gps_lats[start_idx], (int)(gps_lats.size() - start_idx));
                }
                // 2. Рисуем красную линию (Network)
                if (network_line_btn) {
                    size_t start_idx = 0;
                    int net_color_offset = 0;

                    for (size_t i = 1; i < net_lats.size(); ++i) {
                        if (std::abs(net_times[i] - net_times[i - 1]) > 3600000) {
                            ImVec4 col = ImPlot::GetColormapColor(net_color_offset % ImPlot::GetColormapSize());
                            ImPlot::SetNextLineStyle(col, 5.0f);
                            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6.0f);
                            ImPlot::PlotLine(("Net Trip " + std::to_string(net_color_offset)).c_str(), 
                                            &net_lons[start_idx], &net_lats[start_idx], (int)(i - start_idx));
                            start_idx = i;
                            net_color_offset++;
                        }
                    }
                    ImPlot::SetNextLineStyle(ImPlot::GetColormapColor(net_color_offset % ImPlot::GetColormapSize()), 5.0f);
                    ImPlot::PlotLine("Net Active", &net_lons[start_idx], &net_lats[start_idx], (int)(net_lats.size() - start_idx));
                }
                double radius_for_weight;
                double lat_mid = 55.0; // Средняя широта (Новосибирск)
                if (!net_lats.empty()) lat_mid = net_lats[0];
                double lon_scale = cos(lat_mid * 3.1415926535 / 180.0);

                // --- 3. ОТДЕЛЬНЫЙ БЛОК ДЛЯ КРУГОВ ---
                if (radius_btn) {
                    for (size_t i = 0; i < net_lats.size(); ++i) {
                        double freq = calculate_lte_frequency(band_for_circles[i], earfcn_for_circles[i]);
                        double radius_km = calculate_hata_radius(net_rsrp[i], freq, (double)hb_slider);
                        
                        // Масштабируем градусы: широта фиксирована, долгота зависит от косинуса
                        double r_lat_deg = radius_km / 111.1;
                        double r_lon_deg = radius_km / (111.1 * lon_scale);

                        // ImPlot рисует круги в пикселях. Чтобы круг не был овалом на карте,
                        // берем вертикальный радиус (по широте) для отрисовки Circle
                        ImVec2 center = ImPlot::PlotToPixels(ImPlotPoint(net_lons[i], net_lats[i]));
                        ImVec2 edge = ImPlot::PlotToPixels(ImPlotPoint(net_lons[i], net_lats[i] + r_lat_deg));
                        float r_pixels = std::abs(edge.y - center.y);

                        int color_idx = std::abs(net_pcis[i]) % ImPlot::GetColormapSize();
                        ImVec4 base_color = ImPlot::GetColormapColor(color_idx);
                        ImU32 fill_color = IM_COL32((int)(base_color.x * 255), (int)(base_color.y * 255), (int)(base_color.z * 255), 15); 
                        ImU32 line_color = IM_COL32((int)(base_color.x * 255), (int)(base_color.y * 255), (int)(base_color.z * 255), 180);

                        ImPlot::GetPlotDrawList()->AddCircleFilled(center, r_pixels, fill_color);
                        ImPlot::GetPlotDrawList()->AddCircle(center, r_pixels, line_color, 0, 1.2f);
                    }
                }

                // --- 4. ПОИСК БС: ГЕОМЕТРИЧЕСКАЯ ОПТИМИЗАЦИЯ ---
                {
                    std::map<int, std::vector<size_t>> groups;
                    for (size_t i = 0; i < net_lats.size(); ++i) groups[net_pcis[i]].push_back(i);

                    for (auto const& [pci, idxs] : groups) {
                        if (pci <= 0 || idxs.size() < 3) continue;

                        double best_lat = 0, best_lon = 0, max_rsrp = -200;
                        for (size_t idx : idxs) {
                            if (net_rsrp[idx] > max_rsrp) {
                                max_rsrp = net_rsrp[idx];
                                best_lat = net_lats[idx];
                                best_lon = net_lons[idx];
                            }
                        }

                        double hb = (double)hb_slider;
                        for (int iter = 0; iter < 200; ++iter) { // 200 итераций при старте с Max RSRP достаточно
                            double grad_lat = 0, grad_lon = 0;
                            for (size_t idx : idxs) {
                                double freq = calculate_lte_frequency(band_for_circles[idx], earfcn_for_circles[idx]);
                                double r_km = calculate_hata_radius(net_rsrp[idx], freq, hb);
                                
                                // Переводим разницу координат в КИЛОМЕТРЫ для честного градиента
                                double dy_km = (best_lat - net_lats[idx]) * 111.1;
                                double dx_km = (best_lon - net_lons[idx]) * (111.1 * lon_scale);
                                double dist_km = std::sqrt(dx_km * dx_km + dy_km * dy_km);
                                
                                if (dist_km > 1e-6) {
                                    double diff = dist_km - r_km;
                                    // Ошибка в км, переводим обратно в градиент координат
                                    grad_lat += (diff * (dy_km / dist_km)) / 111.1;
                                    grad_lon += (diff * (dx_km / dist_km)) / (111.1 * lon_scale);
                                }
                            }
                            double learning_rate = 0.5 * (1.0 - (double)iter / 200.0);
                            best_lat -= (grad_lat / idxs.size()) * learning_rate;
                            best_lon -= (grad_lon / idxs.size()) * learning_rate;
                        }

                        // Финальная проверка
                        int actual_intersections = 0;
                        for (size_t idx : idxs) {
                            double freq = calculate_lte_frequency(band_for_circles[idx], earfcn_for_circles[idx]);
                            double r_km = calculate_hata_radius(net_rsrp[idx], freq, hb);
                            
                            double dy = (best_lat - net_lats[idx]) * 111.1;
                            double dx = (best_lon - net_lons[idx]) * (111.1 * lon_scale);
                            double dist_km = std::sqrt(dx * dx + dy * dy);

                            if (std::abs(dist_km - r_km) < r_km * 0.25) actual_intersections++; // Допуск 25% для города
                        }

                        if (actual_intersections >= 3) {
                            ImVec4 pci_color = ImPlot::GetColormapColor(std::abs(pci) % ImPlot::GetColormapSize());
                            ImPlot::SetNextMarkerStyle(ImPlotMarker_Up, 16.0f, pci_color, 2.0f, ImVec4(1,1,1,1));
                            ImPlot::PlotScatter(("BS " + std::to_string(pci)).c_str(), &best_lon, &best_lat, 1);
                            
                            std::string label = "PCI: " + std::to_string(pci) + "\neNB: " + std::to_string(net_cis[idxs[0]] / 256);
                            ImPlot::PlotText(label.c_str(), best_lon, best_lat, ImVec2(0, 30));
                        }
                    }
                }
                
                ImPlot::EndPlot();
            }
        
        ImGui::EndTabItem() ;
            }
           if (ImGui::BeginTabItem("GRAPHS")) { 
    
                static std::map<int, std::vector<float>> debug_map;
                if (data_mutex.try_lock()) {
                    debug_map = pci_rsrp_map;
                    data_mutex.unlock();
                }
                

                ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), "Cellular Telemetry Overview");
                ImGui::Text("Active PCIs detected: %d", (int)debug_map.size());
                
                ImGui::Separator();


                static bool all_lines_visible = true;
                
                std::string btn_label = all_lines_visible ? "Hide All Lines" : "Show All Lines";
                
                if (ImGui::Button(btn_label.c_str(), ImVec2(-1, 30))) {
                    all_lines_visible = !all_lines_visible;
                }

                ImGui::Separator();

                if (ImPlot::BeginPlot("##LTE_RSRP_Plot", ImVec2(-1, -1), ImPlotFlags_None)) {
                    

                    ImPlot::SetupAxes("Timeline Index", "Signal Strength (dBm)");
                    ImPlot::SetupAxis(ImAxis_X1, "Index", ImPlotAxisFlags_None);
                    ImPlot::SetupAxis(ImAxis_Y1, "dBm", ImPlotAxisFlags_None);
                    
                    ImPlot::SetupAxesLimits(0, 1000, -140, -40, ImGuiCond_FirstUseEver);

                    int color_idx = 0; 
                    
                    for (const auto& [pci, rsrp_vec] : debug_map) {
                        if (rsrp_vec.empty()) continue;
                        
                        std::string label = "PCI " + std::to_string(pci);
                        int count = static_cast<int>(rsrp_vec.size());
                        
                        ImVec4 line_color = ImPlot::GetColormapColor(color_idx % ImPlot::GetColormapSize());
                        
                        ImPlot::SetNextLineStyle(line_color, 2.0f);
                        
                        
                        if (all_lines_visible) {
                            ImPlot::PlotLine(label.c_str(), rsrp_vec.data(), count);
                        }
                        
                        color_idx++; 
                    }
                    ImPlot::EndPlot();
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
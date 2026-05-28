double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    double dx = 111320.0 * (lon2 - lon1) * std::cos(lat1 * 0.01745);
    double dy = 110540.0 * (lat2 - lat1);
    return std::sqrt(dx * dx + dy * dy);
}

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

Color gradientColor(int ratio) {
    // Ограничиваем диапазон 0-767
    ratio = ratio % 768;

    uint8_t r = 0, g = 0, b = 0, a = 0;

    if (ratio < 256) {
        // Этап 1: Синий -> Голубой (B=255, G растет)
        r = 0;
        g = ratio;
        b = 255;
        a = ratio/2;
    } 
    else if (ratio < 512) {
        // Этап 2: Голубой -> Желтый (B падает, R растет)
        int local = ratio - 256;
        r = local;
        g = 255;
        b = 255 - local;
        a = 127;
    } 
    else {
        // Этап 3: Желтый -> Красный (G падает)
        int local = ratio - 512;
        r = 255;
        g = 255 - local;
        b = 0;
        a = 127;
    }

    return Color(r, g, b, a); 
}




void generate_heat_map_tile(int z, int x, int y, int filter_pci) {
    int max_tile = (int)std::pow(2, z) - 1;
    if (x < 0 || x > max_tile || y < 0 || y > max_tile) return;

    // 1. ИСПРАВЛЕННЫЙ ПУТЬ: учитываем вложенность папок zoom/x/y
    std::string folder_base = (filter_pci == -1) ? "heatTiles/all/" : "heatTiles/pci_" + std::to_string(filter_pci) + "/";
    std::string full_folder_path = folder_base + std::to_string(z) + "/" + std::to_string(x) + "/";
    std::string path = full_folder_path + std::to_string(y) + ".png";

    if (std::filesystem::exists(path)) return;
    
    // Создаем всю цепочку директорий
    std::filesystem::create_directories(full_folder_path);

    const int w = 256;
    const int h = 256;
    const int channels = 4;
    std::vector<unsigned char> image(w * h * channels, 0);

    struct HeatPoint { double px, py; double rsrp; };
    std::vector<HeatPoint> points;

    {
        std::lock_guard<std::mutex> lock(data_mutex);
        for (size_t i = 0; i < net_lats.size(); ++i) {
            
            // 2. ДОБАВЛЕНО: Фильтрация по PCI
            // Если filter_pci != -1, берем только точки этого PCI
            if (filter_pci != -1 && net_pcis[i] != filter_pci) continue;

            double f_x = lon2tile(net_lons[i], z);
            double f_y = lat2tile(net_lats[i], z);

            // Берем точки из текущего тайла + запас для плавности на краях
            if (f_x >= x - 0.5 && f_x <= x + 1.5 && f_y >= y - 0.5 && f_y <= y + 1.5) {
                points.push_back({ (f_x - x) * 256.0, (f_y - y) * 256.0, (double)net_rsrp[i] });
            }
        }
    }

    // Если для данного PCI в этой области нет данных - рисуем прозрачный или пустой тайл
    if (points.empty()) {
        // Оставляем image заполненным нулями (полная прозрачность), 
        // чтобы не перекрывать основную карту, если данных нет
        stbi_write_png(path.c_str(), w, h, channels, image.data(), w * channels);
        return;
    }

    const double p = 4.0; 
    
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            double sum_weighted_rsrp = 0.0;
            double sum_weights = 0.0;

            for (const auto& pt : points) {
                double dx = px - pt.px;
                double dy = py - pt.py;
                double d2 = dx * dx + dy * dy;

                if (d2 < 0.1) {
                    sum_weighted_rsrp = pt.rsrp;
                    sum_weights = 1.0;
                    break;
                }

                double weight = 1.0 / std::pow(d2, p / 2.0);
                sum_weights += weight;
                sum_weighted_rsrp += pt.rsrp * weight;
            }

            double background_rsrp = -110.0;
            double background_weight = 0.00001; 
            
            double final_rsrp = (sum_weighted_rsrp + background_rsrp * background_weight) / (sum_weights + background_weight);

            double norm = (final_rsrp + 110.0) / 30.0; 
            norm = std::clamp(norm, 0.0, 1.0);

            Color c = gradientColor((int)(norm * 767.0));

            int idx = (py * w + px) * channels;
            image[idx + 0] = c.r;
            image[idx + 1] = c.g;
            image[idx + 2] = c.b;
            image[idx + 3] = c.a; 
        }
    }

    stbi_write_png(path.c_str(), w, h, channels, image.data(), w * channels);
}
void thread_loader(int z, int x, int y) {
    download_tile_cpr(z, x, y);
    //generate_heat_map_tile(z,x,y);
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





void refresh_plot_data() {

    std::filesystem::remove_all("heatTiles/");
    try {
        pqxx::connection conn("dbname=location_db user=fel0ne password=123");
        pqxx::read_transaction R(conn);
        

        // 1. ДОБАВЛЯЕМ recorded_time В SQL ЗАПРОС
        auto rows = R.exec(R"(
            SELECT lh.latitude, lh.longitude, lh.provider, cd.signal_strength, cd.extra_data, lh.recorded_time 
            FROM location_history lh
            LEFT JOIN cellular_data cd ON lh.id = cd.location_id
            WHERE lh.accuracy < 100
            ORDER BY lh.id ASC
        )");

        std::lock_guard<std::mutex> lock(data_mutex);
        
        // 2. ОЧИЩАЕМ ВЕКТОРЫ ВРЕМЕНИ (без этого отрисовка сломается)
        gps_lats.clear(); gps_lons.clear(); gps_times.clear();
        net_lats.clear(); net_lons.clear(); net_rsrp.clear(); net_pcis.clear(); net_times.clear();
        pci_rsrp_map.clear(); 
        earfcn_for_circles.clear();
        band_for_circles.clear();
        net_cis.clear();

        double last_gps_lat = 0, last_gps_lon = 0;
        long long last_gps_time = 0;
        double last_net_lat = 0, last_net_lon = 0;
        long long last_net_time = 0;

        for (auto row : rows) {
            double lat = row[0].as<double>();
            double lon = row[1].as<double>();
            std::string provider = row[2].as<std::string>();
            float signal = row[3].is_null() ? -140.0f : row[3].as<float>();
            
            // 3. ИНИЦИАЛИЗИРУЕМ r_time ИЗ 5-й КОЛОНКИ (lh.recorded_time)
            long long r_time = row[5].as<long long>();

            if (provider == "gps") {
                bool is_valid = true;
                if (last_gps_lat != 0.0) {
                    double dist = calculateDistance(last_gps_lat, last_gps_lon, lat, lon);
                    double time_diff = std::abs(r_time - last_gps_time) / 1000.0; // в секундах

                    // КРИТИЧЕСКОЕ УСЛОВИЕ:
                    // Если прыжок огромный, но прошло больше 5 минут (300 сек), 
                    // значит это не баг, а мы просто переместились с выключенным логгером.
                    if (dist > 500.0 && time_diff < 300.0) {
                        is_valid = false; 
                    }
                }

                if (is_valid) {
                    gps_lats.push_back(lat);
                    gps_lons.push_back(lon);
                    gps_times.push_back(r_time);
                    last_gps_lat = lat;
                    last_gps_lon = lon;
                    last_gps_time = r_time;
                }
            } else {
                bool is_valid = true;
                if (last_net_lat != 0.0) {
                    double dist = calculateDistance(last_net_lat, last_net_lon, lat, lon);
                    double time_diff = std::abs(r_time - last_net_time) / 1000.0;

                    if (dist > 800.0 && time_diff < 300.0) {
                        is_valid = false;
                    }
                }
                if(is_valid){
                    net_lats.push_back(lat);
                    net_lons.push_back(lon);
                    net_times.push_back(r_time); // И здесь тоже
                    net_rsrp.push_back((double)signal);
                    
                    int pci_val = 0;
                    int temp_f = 0;
                    int temp_band = 0;
                    if (!row[4].is_null()) {
                        try {
                            auto extra = nlohmann::json::parse(row[4].as<std::string>());
                            if (extra.contains("identity") && extra["identity"].contains("pci")) {
                                pci_val = extra["identity"]["pci"].get<int>();
                                temp_f = extra["identity"]["earfcn"].get<int>();
                                temp_band = extra["identity"]["band"].get<int>();
                                net_cis.push_back(extra["identity"]["ci"].get<long long>());

                            }
                        } catch (...) {}
                    }
                    
                    net_pcis.push_back(pci_val);
                    last_net_lat = lat;
                    last_net_lon = lon;
                    last_net_time = r_time;
                    earfcn_for_circles.push_back(temp_f);
                    band_for_circles.push_back(temp_band);
                    //printf("Read: PCI=%d, Band=%d, Freq_idx=%d\n", pci_val, temp_band, temp_f);
                } 
            }
            
            // --- 2. ЛОГИКА ДЛЯ МУЛЬТИ-ГРАФИКА  ---
            if (!row[4].is_null()) {
                try {
                    auto extra = nlohmann::json::parse(row[4].as<std::string>());
                    
                    int pci = -1;
                    float rsrp_val = -140.0f; // Дефолтное значение

                    // 1. Достаем PCI
                    if (extra.contains("identity") && extra["identity"].contains("pci")) {
                        pci = extra["identity"]["pci"].get<int>();
                    }
                    
                    // 2. Достаем RSRP из объекта signal
                    if (extra.contains("signal") && extra["signal"].contains("rsrp")) {
                        rsrp_val = extra["signal"]["rsrp"].get<float>();
                        
                        // Валидация: Android иногда присылает 2147483647 (Integer.MAX_VALUE), если сигнал недоступен
                        if (rsrp_val > 0) {
                            rsrp_val = -140.0f;
                        }
                    }
                    
                    // Если PCI валидный, кладем его в соответствующую линию
                    if (pci != -1) {
                        pci_rsrp_map[pci].push_back(rsrp_val);
                    }
                } catch (const nlohmann::json::parse_error&) {
                    // Игнорируем битые JSON в БД
                    continue;
                }

            }
            
        }
        
        std::cout << "[DB] Успешно загружено точек из базы: " << rows.size() << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[DB Error in refresh] " << e.what() << std::endl;
    }
}
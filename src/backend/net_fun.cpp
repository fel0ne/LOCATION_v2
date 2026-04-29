std::string make_grid_key(double lat, double lon) { // генерируем ключ чтобы в будущем менять значение в базе если оно же есть в неком радиусе
    std::stringstream ss;
    // Округляем до 4-5 знаков после запятой (шаг ~11 метров)
    ss << std::fixed << std::setprecision(4) << lat << ":" << lon; //хи хи питону привет
    return ss.str();
}
float calculate_lte_frequency(int band, int earfcn) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        if (LTE_TABLE[i].band == band) {
            return LTE_TABLE[i].f_dl_low + 0.1f * (earfcn - LTE_TABLE[i].n_offs_dl);
        }
    }
    return -1.0f; // Если бэнд не найден в таблице
}

void import_from_jsonl(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    pqxx::connection conn("dbname=location_db user=fel0ne password=123");
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            auto json = nlohmann::json::parse(line);
            pqxx::work W(conn); // Транзакция на каждую строку

            std::string grid_key = make_grid_key(json["latitude"], json["longitude"]);

            // Вставляем локацию (копия твоей логики из run_host)
            pqxx::result res_db = W.exec_params(
                R"(INSERT INTO location_history 
                (latitude, longitude, accuracy, provider, source, recorded_time, event_timestamp, grid_key) 
                VALUES ($1, $2, $3, $4, $5, $6, $7, $8) 
                RETURNING id;)",
                json["latitude"].get<double>(),
                json["longitude"].get<double>(),
                json.value("accuracy", 0.0),
                json.value("provider", "unknown"),
                json.value("source", "unknown"),
                json["recordedTime"].get<long long>(), // Используй .value() для безопасности
                json.value("timestamp", 0LL),
                grid_key 
            );

            int location_id = res_db[0][0].as<int>();

            // Вставляем телефонию (копия твоей логики из run_host)
            if (json.contains("telephony") && json["telephony"].is_object()) {
                auto tel = json["telephony"];
                for (auto& [type, content] : tel.items()) {
                    long long cid = 0;
                    float signal = 0.0f;
                    std::string mcc = "0", mnc = "0";

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
                        location_id, type, cid, mcc, mnc, signal, content.dump() 
                    );
                }
            }
            W.commit();
        } catch (...) { continue; } // Пропускаем битые строки
    }
    refresh_plot_data(); // Обновляем GUI после завершения
}
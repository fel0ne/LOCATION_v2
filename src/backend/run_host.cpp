


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
                grid_key TEXT );
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
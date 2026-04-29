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
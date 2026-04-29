#define LOG_FILE "data_log.json"

std::string connect_IP = ""; 

bool is_host_running = false;

const LteBandParams LTE_TABLE[] = {
    {1,  2110.0, 0},
    {3,  1805.0, 1200},
    {7,  2620.0, 2750},  
    {20, 791.0,  6150},
    {31, 462.5,  9870},
    {38, 2570.0, 37750}, 
    {40, 2300.0, 38650}
};
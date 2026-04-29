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

typedef struct {
    int band;
    float f_dl_low;
    int n_offs_dl;
} LteBandParams;
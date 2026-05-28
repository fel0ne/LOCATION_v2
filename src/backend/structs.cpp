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


class HO {
    public:
        int f_pci;
        int s_pci;
        double lat;
        double lon;

        HO(
            int f_pcit,
            int s_pcit,
            double latt,
            double lont
        ){
            f_pci = f_pcit;
            s_pci = s_pcit;
            lat = latt;
            lon = lont;
        }

};
struct Color {
    unsigned char  r, g, b, a;
};

int TABLE_SIZE = (sizeof(LTE_TABLE) / sizeof(LTE_TABLE[0]));

std::vector<double> plot_lats;
std::vector<double> plot_lons;
std::vector<double> plot_rsrp; 
std::vector<double> gps_lats, gps_lons;
std::vector<long long> gps_times; 
std::vector<double> net_lats, net_lons, net_rsrp;
std::vector<long long> net_times;
std::vector<int> net_pcis;
std::vector<long long> net_cis;
std::map<int, std::vector<float>> pci_rsrp_map;
std::vector<int> earfcn_for_circles;
std::vector<int> band_for_circles;
std::mutex data_mutex;
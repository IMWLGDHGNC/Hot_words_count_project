#include "Jieba.hpp"
// Minimal HotWords app with INI config loading and cppjieba usage.
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>


struct Config {
    std::string inputFile;
    std::string outputFile;
    std::string dictDir;
    std::string jiebamode;
    int topk;
    int time_range;
};

bool ReadUtf8Lines(const std::string& filename, std::vector<std::string>& lines) {
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        // \r is left but \n is removed by getline
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return true;
}

//using "/" to split words
std::string Join(const std::vector<std::string>& items, const std::string& delim) {
    std::ostringstream oss;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) oss << delim;
        oss << items[i];
    }
    return oss.str();
}

//removing the spaces/tabs/newlines at head and tail
static std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static bool ParseBool(const std::string& v) {
    std::string t;
    for (char c : v) t.push_back(std::tolower(static_cast<unsigned char>(c)));
    return (t == "true" || t == "1" || t == "yes");
}

static bool LoadIni(const std::string& path, Config& cfg) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(t.substr(0, eq));
        std::string val = Trim(t.substr(eq + 1));
        if (key == "input_file") cfg.inputFile = val;
        else if (key == "output_file") cfg.outputFile = val;
        else if (key == "dict_dir") cfg.dictDir = val;
        else if (key == "mode") cfg.jiebamode = val;
        else if (key == "topk") cfg.topk = std::atoi(val.c_str());//atoi: string->int
        else if (key == "time_range") cfg.time_range = std::atoi(val.c_str());
    }
    return true;
}

int main(int argc, char* argv[]) {
    Config cfg;

    std::string configPath = std::string(PROJECT_ROOT_DIR) + "/config.ini";
    LoadIni(configPath, cfg);

    if (argc >= 2) cfg.inputFile = argv[1];
    if (argc >= 3) cfg.outputFile = argv[2];

    std::string mainDict = std::string(JIEBA_DICT_DIR) + "/jieba.dict.utf8";
    std::string hmmModel = std::string(JIEBA_DICT_DIR) + "/hmm_model.utf8";
    std::string userDict = std::string(JIEBA_DICT_DIR) + "/user.dict.utf8";
    std::string idfFile  = std::string(JIEBA_DICT_DIR) + "/idf.utf8";
    std::string stopFile = std::string(JIEBA_DICT_DIR) + "/stop_words.utf8";

    cppjieba::Jieba jieba(mainDict, hmmModel, userDict, idfFile, stopFile);

    std::vector<std::string> lines;
    std::string inputpath = std::string(INPUT_ROOT_DIR) + "/" + cfg.inputFile;
    if (!ReadUtf8Lines(inputpath, lines)) {
        std::cerr << "[ERROR] cannot open input file: " << inputpath << std::endl;
        return EXIT_FAILURE;
    } else {
        std::cout << "[INFO] read " << lines.size() << " lines from " << inputpath << std::endl;
    }
    
}

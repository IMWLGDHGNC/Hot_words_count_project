#pragma once
#include "Jieba.hpp"
// Minimal HotWords app with INI config loading and cppjieba usage.
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <queue>
#include <map>
#include<string>
#include<deque>
#include<unordered_map>
#include<unordered_set>
#include<stdlib.h>
#include "utf8.h"
#include <stdexcept> // 需要引入异常头文件
#ifdef _WIN32
#include <windows.h>
#endif


typedef long long ll;

void normalize_radicals(std::string& s) {
    std::u32string u32;

    // UTF-8 -> UTF-32
    utf8::utf8to32(s.begin(), s.end(), std::back_inserter(u32));

    for (auto& ch : u32) {
        if (ch == 0x2F08) ch = 0x4EBA; // ⼈ -> 人
        if (ch == 0x2F2F) ch = 0x5DE5; // ⼯ -> 工
    }

    // UTF-32 -> UTF-8
    std::string result;
    utf8::utf32to8(u32.begin(), u32.end(), std::back_inserter(result));

    s.swap(result);
}

struct Config {
    std::string inputFile;
    std::string outputFile;
    std::string dictDir;
    std::string jiebamode;
    int topk;
    int time_range;
    int work_type;
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
std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool LoadIni(const std::string& path, Config& cfg) {
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
        else if (key == "work_type") cfg.work_type = std::atoi(val.c_str());
    }
    return true;
}

std::string extractAction(const std::string& sentence){
    int start = sentence.find('[');
    int end = sentence.find(']',start);
    if(start == std::string::npos) return "";
    else return sentence.substr(start+1,end-1);
}

std::string extractSentence(const std::string& sentence){
    int start = sentence.find(']');
    if(start == std::string::npos) return sentence;
    else return Trim(sentence.substr(start+1));
}

bool checkTime(const std::string& time_str, int &h, int &m, int &s) {
    if (time_str.empty()) return false;
    if (std::sscanf(time_str.c_str(), "%d:%d:%d", &h, &m, &s) == 3) {
        //加上范围检查
        if (h >= 0 && h <= 23 && m >= 0 && m <= 59 && s >= 0 && s <= 59) {
            return true;
        }
    }
    return false;
}

long long check_start_time(const std::string& s) {
    size_t pos = s.find("K=");
    if (pos == std::string::npos) return -1;

    pos += 2; // skip "K="
    size_t end = pos;
    while (end < s.size() && isdigit(s[end])) end++;

    if (end == pos) return -1; // no digits

    return std::stoll(s.substr(pos, end - pos));//stoll: string->long long
}

void scan_stop_words(std::unordered_set<std::string>& stop_words_set){
    // scan for stop words
    std::vector<std::string> stopword_lines;
    std::string stopwordpath = std::string(JIEBA_DICT_DIR) + "/stop_words.utf8";
    if (!ReadUtf8Lines(stopwordpath, stopword_lines)) {
        std::cerr << "[ERROR] cannot open stop word file: " << stopwordpath << std::endl;
        return ;
    } else {
        for (auto& word : stopword_lines) {
            stop_words_set.insert(word);
        }
    }
}

void scan_sensitive_words(std::unordered_set<std::string>& stop_words_set){
    // sensitive words eliminate
    std::string sensitive_words_path = std::string(INPUT_ROOT_DIR) + "/sensitive_words.txt";
    std::vector<std::string> sensitive_vec;
    if (ReadUtf8Lines(sensitive_words_path, sensitive_vec)) {
        for (auto& word : sensitive_vec) {
            stop_words_set.insert(word);
        }
    }
}

void scan_tag_allowed(std::unordered_set<std::string>& tag_allowed_set){
    // tag allowed scan
    std::string tag_allowed_path = std::string(INPUT_ROOT_DIR) + "/tag.txt";
    std::vector<std::string> tag_allowed_vec;
    if (ReadUtf8Lines(tag_allowed_path, tag_allowed_vec)) {
        for (auto& tag : tag_allowed_vec) {
            tag_allowed_set.insert(tag);
        }
    }
}

// Parse window size command like: "WINDOW_SIZE = 10"; return minutes or -1 if absent/invalid
long long check_window_size(const std::string& s) {
    std::string t = s;
    // Normalize spaces
    // Look for keyword
    size_t pos = t.find("WINDOW_SIZE");
    if (pos == std::string::npos) return -1;
    // Move to '=' after keyword
    size_t eq = t.find('=', pos);
    if (eq == std::string::npos) return -1;
    // Skip spaces after '='
    size_t i = eq + 1;
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) i++;
    // Parse number
    size_t j = i;
    while (j < t.size() && isdigit(static_cast<unsigned char>(t[j]))) j++;
    if (j == i) return -1;
    try {
        long long v = std::stoll(t.substr(i, j - i));
        return v;
    } catch (...) {
        return -1;
    }
}
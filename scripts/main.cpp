#include"utils.hpp"
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

static double get_memory_mb(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if(GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))){
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
#endif
    return 0.0;
}

int deal_with_file_input(cppjieba::Jieba& jieba, const Config& cfg) {
    using Clock = std::chrono::steady_clock;
    auto t_begin = Clock::now();
    long long processed_lines = 0;
    long long processing_ms = 0;
    std::vector<std::string> lines;

    std::string inputpath = std::string(INPUT_ROOT_DIR) + "/" + cfg.inputFile;
    std::string outputpath = std::string(OUTPUT_ROOT_DIR) + "/" + cfg.outputFile;

    std::ofstream out(outputpath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[ERROR] cannot open output file: " << outputpath << std::endl;
        return EXIT_FAILURE;
    }

    out << "===== cppjieba segmentation =====";
    out << "\nInputFile: " << inputpath << "\n";
    out << "OutputFile: " << outputpath << "\n";
    out << "JiebaMode: " << cfg.jiebamode << "\n";

    std::unordered_map<std::string, std::string> word_tag_map;
    std::unordered_map<std::string, int> word_count_map;
    std::multimap<ll, std::string> window_index; // 有序窗口索引，处理迟到/乱序
    std::multimap<ll, std::string> history_map;  // 有序历史索引，支持任意时刻查询
    
    std::unordered_set<std::string> stop_words_set;
    std::unordered_set<std::string> tag_allowed_set;
    scan_stop_words(stop_words_set);
    scan_sensitive_words(stop_words_set);
    scan_tag_allowed(tag_allowed_set);

    // 读取输入文件
    if (!ReadUtf8Lines(inputpath, lines)) {
        std::cerr << "[ERROR] cannot open input file: " << inputpath << std::endl;
        return EXIT_FAILURE;
    } else if (lines.empty()) {
        std::cout << "[INFO] input file is empty: " << inputpath << std::endl;
        return EXIT_FAILURE;
    } else {
        std::cout << "[INFO] read " << lines.size() << " lines from " << inputpath << std::endl;
        out << "LineCount: " << lines.size() << "\n";
    }

    auto cmp = [&](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
        if (a.second == b.second) return a.first > b.first; // 字典序逆序，让'a'排在'b'前面
        return a.second < b.second; // 频次正序，让大频次排在上面
    };

    ll currtime = 0; // 当前流的时间（秒）
    int current_time_range = cfg.time_range; // 可动态调整的窗口大小（分钟）

    for (size_t idx = 0; idx < lines.size(); ++idx) {
        auto iter_begin = Clock::now();
        std::string contents = lines[idx];
        normalize_radicals(contents);
        std::string action_str = extractAction(contents);
        int h, m, s;
        ll queryTime = -1;

        bool is_data_line = checkTime(action_str, h, m, s);
        
        if (!is_data_line) {
            std::string require = extractSentence(contents);
            // 支持动态修改窗口大小: WINDOW_SIZE = N
            long long new_win = check_window_size(require);
            if (new_win != -1) {
                if (new_win <= 0) new_win = 1;
                current_time_range = static_cast<int>(new_win);
                out << "[INFO] time_range updated to " << current_time_range << " min\n";
                // 变更窗口后，基于 history_map 立即重建当前窗口的计数与索引，确保随后的查询生效
                {
                    word_count_map.clear();
                    window_index.clear();
                    ll start_time = (currtime >= current_time_range * 60) ? (currtime - current_time_range * 60) : 0;
                    auto it_start = history_map.lower_bound(start_time);
                    auto it_end_rebuild = history_map.upper_bound(currtime);
                    for (auto it = it_start; it != it_end_rebuild; ++it) {
                        const std::string &w = it->second;
                        word_count_map[w]++;
                        window_index.insert({it->first, w});
                    }
                }
                // 仅修改窗口，不进行查询
                continue;
            }
            queryTime = check_start_time(require);
            if (queryTime == -1) {
                out << "[WARNING] Line " << idx + 1 << ": cannot extract valid time info.\n";
                continue;
            }
        }

        if (is_data_line) {
            ll new_time = h * 3600 + m * 60 + s;
            if (new_time > 86400 || new_time < 0) {
                out << "[WARNING] Line " << idx + 1 << ": time " << h << ":" << m << ":" << s << " is out of range.\n";
                continue;
            }
            if (new_time >= currtime) currtime = new_time;

            std::string sentence = extractSentence(contents);
            std::vector<std::pair<std::string, std::string>> tagres;
            jieba.Tag(sentence, tagres);

            for (auto& v : tagres) {
                if (!tag_allowed_set.empty() && tag_allowed_set.find(v.second) == tag_allowed_set.end()) continue;
                if (stop_words_set.find(v.first) != stop_words_set.end()) continue;
                word_tag_map[v.first] = v.second;
                history_map.insert({new_time, v.first});
                window_index.insert({new_time, v.first});
                word_count_map[v.first]++;
            }

            // 维护滑动窗口 (移除过期数据，按时间有序淘汰，支持迟到/乱序)
            ll threshold_time = (currtime >= current_time_range * 60) ? (currtime - current_time_range * 60) : 0;
            auto it_end = window_index.lower_bound(threshold_time);
            for (auto it = window_index.begin(); it != it_end; ++it) {
                auto wc = word_count_map.find(it->second);
                if (wc != word_count_map.end()) {
                    wc->second--;
                    if (wc->second <= 0) {
                        word_count_map.erase(wc);
                    }
                }
            }
            window_index.erase(window_index.begin(), it_end);

        } else {
            // ===== 处理查询行 =====
            ll qtime_seconds = queryTime * 60;
            out << "Query Time: " << queryTime << " minute" << "\n";

            std::priority_queue<std::pair<std::string, int>, std::vector<std::pair<std::string, int>>, decltype(cmp)> pq(cmp);
            bool is_current_window = (currtime >= qtime_seconds) && (currtime - qtime_seconds < 60);
            if (is_current_window) {
                for (auto& p : word_count_map) pq.push(p);
            } else {
                ll start_time = (qtime_seconds >= current_time_range * 60) ? (qtime_seconds - current_time_range * 60) : 0;
                std::unordered_map<std::string, int> temp_cnt_map;
                auto it_start = history_map.lower_bound(start_time);
                auto it_end2 = history_map.upper_bound(qtime_seconds + 59);
                for (auto it = it_start; it != it_end2; ++it) temp_cnt_map[it->second]++;
                for (auto& p : temp_cnt_map) pq.push(p);
            }

            for (size_t i = 1; i <= cfg.topk; ++i) {
                if (!pq.empty()) {
                    auto top = pq.top();
                    pq.pop();
                    out << i << ": " << top.first << "/" << word_tag_map[top.first] << "/" << top.second << std::endl;
                } else {
                    break; 
                }
            }
        }

        processed_lines++;
        processing_ms += std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - iter_begin).count();
    }
    
    double elapsed_sec = std::chrono::duration_cast<std::chrono::duration<double>>(Clock::now() - t_begin).count();
    double avg_latency_ms = processed_lines > 0 ? (static_cast<double>(processing_ms) / processed_lines) : 0.0;
    double throughput_lps = elapsed_sec > 0 ? (static_cast<double>(processed_lines) / elapsed_sec) : 0.0;
    double mem_mb = get_memory_mb();

    out << "===================================\n";
    out << "Program Metrics" << "\n";
    out << "Throughput(lines/sec): " << throughput_lps << "\n";
    out << "AvgLatency(ms/line): " << avg_latency_ms << "\n";
    out << "Memory(MB): " << mem_mb << "\n";

    out.close();
    return EXIT_SUCCESS;
}

int deal_with_console_input(cppjieba::Jieba& jieba, const Config& cfg) {
    std::string outputpath = std::string(OUTPUT_ROOT_DIR) + "/" + cfg.outputFile;

    std::ofstream out(outputpath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[ERROR] cannot open output file: " << outputpath << std::endl;
        return EXIT_FAILURE;
    }
    out << "===== cppjieba segmentation =====";
    out << "Choosing console_input_mode\n";
    out << "OutputFile: " << outputpath << "\n";
    out << "JiebaMode: " << cfg.jiebamode << "\n";

    std::unordered_map<std::string, std::string> word_tag_map;
    std::unordered_map<std::string, int> word_count_map;
    std::multimap<ll, std::string> window_index; // 有序窗口索引
    std::multimap<ll, std::string> history_map;  // 有序历史索引
    std::unordered_set<std::string> stop_words_set;
    std::unordered_set<std::string> tag_allowed_set;
    scan_tag_allowed(tag_allowed_set);
    scan_stop_words(stop_words_set);
    scan_sensitive_words(stop_words_set);

    auto cmp = [&](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
        if (a.second == b.second) return a.first > b.first; 
        return a.second < b.second; 
    };

    using Clock = std::chrono::steady_clock;
    long long line_count = 0;
    long long processing_ms = 0;
    int current_time_range = cfg.time_range;
    std::cout << "==========================================================" << std::endl;
    //std::cout << "[IMPORTANT] If on Windows, run 'chcp 65001' first." << std::endl;
    std::cout << "Input format:" << std::endl;
    std::cout << "  1. [HH:MM:SS] Sentence  -> Set explicit time." << std::endl;
    std::cout << "  2. Sentence             -> Use current time (" << current_time_range << " min window)." << std::endl;
    std::cout << "  3. [ACTION] QUERY K=15  -> Query hot words at minute 15." << std::endl;
    std::cout << "  4. [ACTION] WINDOW_SIZE=10 -> Adjust time window to 10 minutes." << std::endl;
    std::cout << "Type 'exit' to quit." << std::endl;
    std::cout << "==========================================================" << std::endl;

    ll currtime = 0;

    while (true) {
        std::string content;
        std::cout << "> ";
        std::getline(std::cin, content);
        
        if (content == "exit") break;
        if (content.empty()) continue;
        if (content.back() == '\r') content.pop_back();

        line_count++;

        try {
            normalize_radicals(content);

            bool is_data_processing = false;
            ll event_time = 0;
            std::string sentence_to_process;

            // 1. 先尝试解析动作/时间
            std::string action_str = extractAction(content);
            int h, m, s;
            bool has_explicit_time = checkTime(action_str, h, m, s);
            
            // 2. 检查是否为查询/窗口大小指令 (只有当没有时间戳时才可能是这些指令)
            ll queryTime = -1;
            if (!has_explicit_time) {
                std::string potential_cmd = extractSentence(content); 
                if (potential_cmd.empty()) potential_cmd = content; // 兼容
                queryTime = check_start_time(potential_cmd);
                // 动态调整窗口大小，如: WINDOW_SIZE = 10
                long long new_win = check_window_size(potential_cmd);
                if (new_win != -1) {
                    if (new_win <= 0) new_win = 1;
                    current_time_range = static_cast<int>(new_win);
                    std::cout << "[INFO] time_range updated to " << current_time_range << " min" << std::endl;
                    out << "[INFO] time_range updated to " << current_time_range << " min\n";
                    // 变更窗口后，基于 history_map 立即重建当前窗口的计数与索引
                    {
                        word_count_map.clear();
                        window_index.clear();
                        ll start_time = (currtime >= current_time_range * 60) ? (currtime - current_time_range * 60) : 0;
                        auto it_start = history_map.lower_bound(start_time);
                        auto it_end_rebuild = history_map.upper_bound(currtime);
                        for (auto it = it_start; it != it_end_rebuild; ++it) {
                            const std::string &w = it->second;
                            word_count_map[w]++;
                            window_index.insert({it->first, w});
                        }
                    }
                    continue; // 本行仅用于调整窗口，不进行分词/查询
                }
            }

            // 3. 核心分支逻辑
            if (queryTime != -1) {
                is_data_processing = false; 
            } 
            else if (has_explicit_time) {
                event_time = h * 3600 + m * 60 + s;
                if (event_time >= currtime) currtime = event_time;
                sentence_to_process = extractSentence(content);
                is_data_processing = true;
            } 
            else {
                event_time = currtime; 
                sentence_to_process = content; 
                is_data_processing = true;
                
                int cur_h = (currtime / 3600) % 24;
                int cur_m = (currtime % 3600) / 60;
                int cur_s = currtime % 60;
                std::cout << "[INFO] No timestamp. Defaulting to current time: " 
                          << cur_h << ":" << cur_m << ":" << cur_s << std::endl;
            }

            // 4. 执行逻辑
            auto iter_begin = Clock::now();
            if (is_data_processing) {
                std::vector<std::pair<std::string, std::string>> tagres;
                jieba.Tag(sentence_to_process, tagres);

                for (auto& v : tagres) {
                    if (!tag_allowed_set.empty() && tag_allowed_set.find(v.second) == tag_allowed_set.end()) 
                        continue;
                    if (stop_words_set.find(v.first) != stop_words_set.end()) continue;
                    word_tag_map[v.first] = v.second;
                    history_map.insert({event_time, v.first});
                    window_index.insert({event_time, v.first});
                    word_count_map[v.first]++;
                }

                ll threshold_time = (currtime >= current_time_range * 60) ? (currtime - current_time_range * 60) : 0;
                auto it_end = window_index.lower_bound(threshold_time);
                for (auto it = window_index.begin(); it != it_end; ++it) {
                    auto wc = word_count_map.find(it->second);
                    if (wc != word_count_map.end()) {
                        wc->second--;
                        if (wc->second <= 0) word_count_map.erase(wc);
                    }
                }
                window_index.erase(window_index.begin(), it_end);
            }
            else {
                // ===== 查询处理逻辑 (Case A) =====
                ll qtime_seconds = queryTime * 60;
                out << "Query Time: " << queryTime << " minute" << "\n";
                std::cout << "Querying Top " << cfg.topk << " words at minute " << queryTime << ", window size = " << current_time_range << " minutes" << std::endl;

                std::priority_queue<std::pair<std::string, int>, std::vector<std::pair<std::string, int>>, decltype(cmp)> pq(cmp);

                // 判断查询时间是否在“当前分钟”内
                bool is_current_window = (currtime >= qtime_seconds) && (currtime - qtime_seconds < 60);

                if (is_current_window) {
                    for (auto& p : word_count_map) pq.push(p);
                } else {
                    ll start_time = (qtime_seconds >= current_time_range * 60) ? (qtime_seconds - current_time_range * 60) : 0;
                    std::unordered_map<std::string, int> temp_cnt_map;
                    auto it_start = history_map.lower_bound(start_time);
                    auto it_end = history_map.upper_bound(qtime_seconds + 59);

                    for (auto it = it_start; it != it_end; ++it) {
                        temp_cnt_map[it->second]++;
                    }
                    for (auto& p : temp_cnt_map) pq.push(p);
                }

                int k = 0;
                if(pq.empty()) std::cout << "No hot words found." << std::endl;
                while (!pq.empty() && k < cfg.topk) {
                    auto top = pq.top();
                    pq.pop();
                    k++;
                    out << k << ": " << top.first << "/" << word_tag_map[top.first] << "/" << top.second << std::endl;
                    std::cout << k << ": " << top.first << "/" << word_tag_map[top.first] << "/" << top.second << std::endl;
                }
            }
            processing_ms += std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - iter_begin).count();

        } catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << std::endl;
        }
    }
    
    double total_proc_sec = static_cast<double>(processing_ms) / 1000.0;
    double avg_latency_ms = line_count > 0 ? (static_cast<double>(processing_ms) / line_count) : 0.0;
    double throughput_lps = total_proc_sec > 0 ? (static_cast<double>(line_count) / total_proc_sec) : 0.0;
    double mem_mb = get_memory_mb();
    
    std::cout<< "保存到文件: " << outputpath << std::endl;
    out << "===================================\n";
    out << "Total lines processed: " << line_count << "\n";
    out << "Program Metrics" << "\n";
    out << "Throughput(lines/sec): " << throughput_lps << "\n";
    out << "AvgLatency(ms/line): " << avg_latency_ms << "\n";
    out << "Memory(MB): " << mem_mb << "\n";
    out.close();
    return EXIT_SUCCESS;
}

int main() {

    #ifdef _WIN32
    SetConsoleOutputCP(65001);//setting the output to utf-8
    SetConsoleCP(65001);//setting the input to utf-8
    // Ensure stdout and stdin are unbuffered for interactive mode
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stdin, nullptr, _IONBF, 0);
    #endif

    Config cfg;
    std::string configPath = std::string(PROJECT_ROOT_DIR) + "/config.ini";
    LoadIni(configPath, cfg);
    int work_type = cfg.work_type;
    if(work_type == 1){
        std::cout<<"Choosing File_Input mode"<<std::endl;
    }else{
        std::cout<<"Choosing Console_Input mode"<<std::endl;
    }

    std::string mainDict = std::string(JIEBA_DICT_DIR) + "/jieba.dict.utf8";
    std::string hmmModel = std::string(JIEBA_DICT_DIR) + "/hmm_model.utf8";
    std::string userDict = std::string(JIEBA_DICT_DIR) + "/user.dict.utf8";
    std::string idfFile  = std::string(JIEBA_DICT_DIR) + "/idf.utf8";
    std::string stopFile = std::string(JIEBA_DICT_DIR) + "/stop_words.utf8";
    
    std::string userterms = std::string(INPUT_ROOT_DIR) + "/user_word.txt";
    std::string sensitive_words = std::string(INPUT_ROOT_DIR) + "/sensitive_words.txt";

    cppjieba::Jieba jieba(mainDict, hmmModel, userDict, idfFile, stopFile);
    
    std::vector<std::string> userterms_vec;
    ReadUtf8Lines(userterms, userterms_vec);

    for(auto &word:userterms_vec){
        jieba.InsertUserWord(word,20000);
    }

    //add user words(proper nouns) to improve the completeness of recognition

    if(work_type==1){
        deal_with_file_input(jieba, cfg);
    } else{
        std::cout << "Console input mode (UTF-8). Type 'exit' to quit." << std::endl;
        deal_with_console_input(jieba, cfg);
    }
    return 0;
    
}

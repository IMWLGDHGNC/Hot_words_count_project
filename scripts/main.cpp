#include"utils.hpp"

int deal_with_file_input(cppjieba::Jieba& jieba, const Config& cfg) {
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
    std::deque<std::pair<ll, std::string>> window_queue;
    std::multimap<ll, std::string> history_map;
    
    std::unordered_set<std::string> stop_words_set;

    // scan for stop words
    std::vector<std::string> stopword_lines;
    std::string stopwordpath = std::string(JIEBA_DICT_DIR) + "/stop_words.utf8";
    if (!ReadUtf8Lines(stopwordpath, stopword_lines)) {
        std::cerr << "[ERROR] cannot open stop word file: " << stopwordpath << std::endl;
        return EXIT_FAILURE;
    } else {
        for (auto& word : stopword_lines) {
            stop_words_set.insert(word);
        }
        out << "StopWordsCount: " << stop_words_set.size() << "\n";
    }

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

    for (size_t idx = 0; idx < lines.size(); ++idx) {
        std::string contents = lines[idx];
        normalize_radicals(contents);
        std::string action_str = extractAction(contents);
        int h, m, s;
        ll queryTime = -1;

        bool is_data_line = checkTime(action_str, h, m, s);
        
        if (!is_data_line) {
            std::string require = extractSentence(contents);
            queryTime = check_start_time(require);
            if (queryTime == -1) {
                out << "[WARNING] Line " << idx + 1 << ": cannot extract valid time info.\n";
                continue;
            }
        }

        if (is_data_line) {
            ll new_time = h * 3600 + m * 60 + s;
            if (new_time >= currtime) currtime = new_time;

            std::string sentence = extractSentence(contents);
            std::vector<std::pair<std::string, std::string>> tagres;
            jieba.Tag(sentence, tagres);

            for (auto& v : tagres) {
                if (stop_words_set.find(v.first) != stop_words_set.end()) continue;
                word_tag_map[v.first] = v.second;
                history_map.insert({new_time, v.first});
                window_queue.push_back({new_time, v.first});
                word_count_map[v.first]++;
            }

            //维护滑动窗口 (移除过期数据)
            // 只有当队列头的时间 < 当前时间 - 时间范围，才移除
            ll threshold_time = (currtime >= cfg.time_range * 60) ? (currtime - cfg.time_range * 60) : 0;
            
            while (!window_queue.empty() && window_queue.front().first < threshold_time) {
                std::string expired_word = window_queue.front().second;
                
                auto it = word_count_map.find(expired_word);
                if (it != word_count_map.end()) {
                    it->second--;
                    if (it->second <= 0) {
                        word_count_map.erase(it);
                    }
                }
                window_queue.pop_front();
            }

        } else {
            // ===== 处理查询行 =====
            ll qtime_seconds = queryTime * 60;
            out << "Query Time: " << queryTime << " minute" << "\n";

            std::priority_queue<std::pair<std::string, int>, std::vector<std::pair<std::string, int>>, decltype(cmp)> pq(cmp);
            bool is_current_window = (currtime >= qtime_seconds) && (currtime - qtime_seconds < 60);
            // 查询时间点就是当前流的时间点 -> 直接使用维护好的 word_count_map
            if (is_current_window) {
                for (auto& p : word_count_map) {
                    pq.push(p);
                }
            } 
            // 查询过去任意时间段 -> 从 history_map 重新构建
            else {
                ll start_time = (qtime_seconds >= cfg.time_range * 60) ? (qtime_seconds - cfg.time_range * 60) : 0;
                std::unordered_map<std::string, int> temp_cnt_map;
                auto it_start = history_map.lower_bound(start_time);
                auto it_end = history_map.upper_bound(qtime_seconds);

                for (auto it = it_start; it != it_end; ++it) {
                    temp_cnt_map[it->second]++;
                }
                
                for (auto& p : temp_cnt_map) {
                    pq.push(p);
                }
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
    }
    
    out.close();
    return EXIT_SUCCESS;
}

// int deal_with_console_input(cppjieba::Jieba& jieba, const Config& cfg) {
//     std::string outputpath = std::string(OUTPUT_ROOT_DIR) + "/" + cfg.outputFile;

//     std::ofstream out(outputpath, std::ios::binary);
//     if (!out.is_open()) {
//         std::cerr << "[ERROR] cannot open output file: " << outputpath << std::endl;
//         return EXIT_FAILURE;
//     }
//     out << "===== cppjieba segmentation =====";
//     out << "Choosing console_input_mode\n";
//     out << "OutputFile: " << outputpath << "\n";
//     out << "JiebaMode: " << cfg.jiebamode << "\n";

//     std::unordered_map<std::string, std::string> word_tag_map;
//     std::unordered_map<std::string, int> word_count_map;
//     std::deque<std::pair<ll, std::string>> window_queue;
//     std::multimap<ll, std::string> history_map;
//     std::unordered_set<std::string> stop_words_set;

//     // scan for stop words
//     std::vector<std::string> stopword_lines;
//     std::string stopwordpath = std::string(JIEBA_DICT_DIR) + "/stop_words.utf8";
//     if (!ReadUtf8Lines(stopwordpath, stopword_lines)) {
//         std::cerr << "[ERROR] cannot open stop word file: " << stopwordpath << std::endl;
//         return EXIT_FAILURE;
//     } else {
//         for (auto& word : stopword_lines) {
//             stop_words_set.insert(word);
//         }
//         out << "StopWordsCount: " << stop_words_set.size() << "\n";
//     }
//     auto cmp = [&](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
//         if (a.second == b.second) return a.first > b.first; // 字典序逆序，让'a'排在'b'前面
//         return a.second < b.second; // 频次正序，让大频次排在上面
//     };

//     //=====input=====
//     int line_count = 0;
//     std::cout<<"======== Hot Words Count (Console Input Mode) ========\n";
//     std::cout<< "Please input sentences line by line. Making sure that the Chinese characters are in utf-8 encoding. \nIf you need to add a line, using the format '[HH:MM:SS] sentence.\n";
//     std::cout<< "e.g. [12:30:45] 今天天气真好！\n";
//     std::cout<< "If you need to query the hot words at a certain time point, please input in the format '[ACTION] QUERY K=T'.\n e.g. [ACTION] QUERY K=15\n";
//     std::cout<< "==================================================\n";
//     std::cout<< "Please input your sentence in console (type 'exit' to quit):" << std::endl;
//     ll currtime=0;
//     while (true) {
//         std:: string content;
//         std::getline(std::cin, content);
//         if (content == "exit") {
//             break;
//         }
//         if (content.empty()) {
//             continue;
//         }
//         line_count++;
//         normalize_radicals(content);

//         std::string action_str = extractAction(content);
//         if (action_str.empty()) new
//         int h, m, s;
//         ll queryTime = -1;

//         bool is_data_line = checkTime(action_str, h, m, s);
//         if(!is_data_line){
//             std::string require = extractSentence(content);
//             queryTime = check_start_time(require);
//             if(queryTime == -1){
//                 std::cout << "[WARNING] Line " << line_count << ": cannot extract valid time info.\n";
//                 out<< "[WARNING] Line " << line_count << ": cannot extract valid time info.\n";
//                 continue;
//             }
//         }

//         if(is_data_line){
//             ll new_time = h * 3600 + m * 60 + s;
//             if (new_time >= currtime) currtime = new_time;
//             std::string sentence = extractSentence(content);
//             std::vector<std::pair<std::string, std::string>> tagres;
//             jieba.Tag(sentence, tagres);
//             for (auto& v : tagres) {
//                 if (stop_words_set.find(v.first) != stop_words_set.end()) continue;
//                 word_tag_map[v.first] = v.second;
//                 history_map.insert({new_time, v.first});
//                 window_queue.push_back({new_time, v.first});
//                 word_count_map[v.first]++;
//             }
//             ll threshold_time = (currtime >= cfg.time_range * 60) ? (currtime - cfg.time_range * 60) : 0;
//             while (!window_queue.empty() && window_queue.front().first < threshold_time) {
//                 std::string expired_word = window_queue.front().second;
//                 auto it = word_count_map.find(expired_word);
//                 if (it != word_count_map.end()) {
//                     it->second--;
//                     if (it->second <= 0) {
//                         word_count_map.erase(it);
//                     }
//                 }
//                 window_queue.pop_front();
//             } 
//         }else{
//             //===== 处理查询行 =====
//             ll qtime_seconds = queryTime * 60;
//             out << "Query Time: " << queryTime << " minute" << "\n";
//             std::priority_queue<std::pair<std::string, int>, std::vector<std::pair<std::string, int>>, decltype(cmp)> pq(cmp);
//             bool is_current_window = (currtime >= qtime_seconds) && (currtime - qtime_seconds < 60);
//             if (is_current_window){
//                 for (auto& p:word_count_map)
//                     pq.push(p);
//             } else {
//                 ll start_time = (qtime_seconds >= cfg.time_range * 60) ? (qtime_seconds - cfg.time_range * 60) : 0;
//                 std::unordered_map<std::string, int> temp_cnt_map;
//                 auto it_start = history_map.lower_bound(start_time);
//                 auto it_end = history_map.upper_bound(qtime_seconds);
//                 for (auto it = it_start; it != it_end; ++it) {
//                     temp_cnt_map[it->second]++;
//                 }
//                 for (auto& p : temp_cnt_map) {
//                     pq.push(p);
//                 }
//             }
//             int k=0;
//             while(!pq.empty()&&k<cfg.topk){
//                 auto top = pq.top();
//                 pq.pop();
//                 k++;
//                 out<< k << ": " << top.first << "/" << word_tag_map[top.first] << "/" << top.second << std::endl;
//                 std::cout<< k << ": " << top.first << "/" << word_tag_map[top.first] << "/" << top.second << std::endl;
//             }
//             std::cout<< "========Done========\n";
//         }

//     }
//     out<< "Total lines processed: " << line_count << "\n";
//     out.close();
//     return EXIT_SUCCESS;
// }

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
    std::deque<std::pair<ll, std::string>> window_queue;
    std::multimap<ll, std::string> history_map;
    std::unordered_set<std::string> stop_words_set;

    // Load stop words
    std::vector<std::string> stopword_lines;
    std::string stopwordpath = std::string(JIEBA_DICT_DIR) + "/stop_words.utf8";
    if (!ReadUtf8Lines(stopwordpath, stopword_lines)) {
        std::cerr << "[ERROR] cannot open stop word file: " << stopwordpath << std::endl;
        return EXIT_FAILURE;
    } else {
        for (auto& word : stopword_lines) {
            stop_words_set.insert(word);
        }
        out << "StopWordsCount: " << stop_words_set.size() << "\n";
    }

    auto cmp = [&](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
        if (a.second == b.second) return a.first > b.first; 
        return a.second < b.second; 
    };

    int line_count = 0;
    std::cout << "==========================================================" << std::endl;
    std::cout << "[IMPORTANT] If on Windows, run 'chcp 65001' first." << std::endl;
    std::cout << "Input format:" << std::endl;
    std::cout << "  1. [HH:MM:SS] Sentence  -> Set explicit time." << std::endl;
    std::cout << "  2. Sentence             -> Use current time (" << cfg.time_range << " min window)." << std::endl;
    std::cout << "  3. [ACTION] QUERY K=15  -> Query hot words at minute 15." << std::endl;
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
            
            // 2. 检查是否为查询指令 (只有当没有时间戳时才可能是查询)
            ll queryTime = -1;
            if (!has_explicit_time) {
                std::string potential_cmd = extractSentence(content); 
                if (potential_cmd.empty()) potential_cmd = content; // 兼容
                queryTime = check_start_time(potential_cmd);
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
            if (is_data_processing) {
                std::vector<std::pair<std::string, std::string>> tagres;
                jieba.Tag(sentence_to_process, tagres);

                for (auto& v : tagres) {
                    if (stop_words_set.find(v.first) != stop_words_set.end()) continue;
                    
                    word_tag_map[v.first] = v.second;
                    history_map.insert({event_time, v.first}); // 存入历史
                    window_queue.push_back({event_time, v.first}); // 存入窗口
                    word_count_map[v.first]++;
                }

                ll threshold_time = (currtime >= cfg.time_range * 60) ? (currtime - cfg.time_range * 60) : 0;
                while (!window_queue.empty() && window_queue.front().first < threshold_time) {
                    std::string expired_word = window_queue.front().second;
                    auto it = word_count_map.find(expired_word);
                    if (it != word_count_map.end()) {
                        it->second--;
                        if (it->second <= 0) word_count_map.erase(it);
                    }
                    window_queue.pop_front();
                }
            } 
            else {
                // ===== 查询处理逻辑 (Case A) =====
                ll qtime_seconds = queryTime * 60;
                out << "Query Time: " << queryTime << " minute" << "\n";
                std::cout << "Querying Top " << cfg.topk << " words at minute " << queryTime << "..." << std::endl;

                std::priority_queue<std::pair<std::string, int>, std::vector<std::pair<std::string, int>>, decltype(cmp)> pq(cmp);

                // 判断查询时间是否在“当前分钟”内
                bool is_current_window = (currtime >= qtime_seconds) && (currtime - qtime_seconds < 60);

                if (is_current_window) {
                    for (auto& p : word_count_map) pq.push(p);
                } else {
                    ll start_time = (qtime_seconds >= cfg.time_range * 60) ? (qtime_seconds - cfg.time_range * 60) : 0;
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

        } catch (const std::exception& e) {
            std::cerr << "[ERROR] " << e.what() << std::endl;
        }
    }
    
    out << "Total lines processed: " << line_count << "\n";
    out.close();
    return EXIT_SUCCESS;
}

int main() {

    #ifdef _WIN32
    SetConsoleOutputCP(65001);//setting the output to utf-8
    SetConsoleCP(65001);//setting the input to utf-8
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
        deal_with_console_input(jieba, cfg);
    }
    return 0;
    
}

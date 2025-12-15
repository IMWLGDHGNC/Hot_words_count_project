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

    // 1. word_tag_map: 记录单词词性
    std::unordered_map<std::string, std::string> word_tag_map;
    
    // 2. word_count_map: 当前滑动窗口内的词频统计 (实时热词)
    std::unordered_map<std::string, int> word_count_map;
    
    // 3. window_queue: 维护滑动窗口的时间序列 (时间, 单词)，用于FIFO移除过期词
    std::deque<std::pair<ll, std::string>> window_queue;

    // 4. history_map: 全量历史记录，用于查询过去任意时间段 (有序，支持范围查找)
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

        // 尝试解析是否为数据行（包含时间）
        bool is_data_line = checkTime(action_str, h, m, s);
        
        if (!is_data_line) {
            // 如果不是数据行，尝试解析是否为查询行
            std::string require = extractSentence(contents);
            queryTime = check_start_time(require);
            if (queryTime == -1) {
                out << "[WARNING] Line " << idx + 1 << ": cannot extract valid time info.\n";
                continue;
            }
        }

        if (is_data_line) {
            ll new_time = h * 3600 + m * 60 + s;
            // 只有当时间向前推进时才更新 currtime (防止乱序导致逻辑回退，通常日志是顺序的)
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

            // 情况A: 查询时间点就是当前流的时间点 -> 直接使用维护好的 word_count_map
            // 允许一定的误差容忍，或者完全相等
            if (qtime_seconds == currtime) {
                for (auto& p : word_count_map) {
                    pq.push(p);
                }
            } 
            // 情况B: 查询过去任意时间段 -> 从 history_map 重新构建
            else {
                ll start_time = (qtime_seconds >= cfg.time_range * 60) ? (qtime_seconds - cfg.time_range * 60) : 0;
                std::unordered_map<std::string, int> temp_cnt_map;
                
                // 使用 multimap 的范围查找 (比 unordered_map 遍历高效得多)
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


int main() {
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
    } 
    return 0;
    
}

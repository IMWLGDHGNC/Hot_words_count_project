#include"utils.hpp"

int deal_with_file_input(cppjieba::Jieba& jieba, const Config& cfg){
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
    std::unordered_multimap<ll, std::string> time_word_map;
    std::unordered_map <std::string, std::string> word_tag_map;
    std::unordered_map <std::string, int> word_count_map;
    std::unordered_set <std::string> stop_words_set;
    std::unordered_multimap <ll, std::string> real_time_word_map;

    //scan the stop word file
    std::vector<std::string> stopword_lines;
    std::string stopwordpath = std::string(JIEBA_DICT_DIR) + "/stop_words.utf8";
    if(!ReadUtf8Lines(stopwordpath, stopword_lines)){
        std::cerr << "[ERROR] cannot open stop word file: " << stopwordpath << std::endl;
        return EXIT_FAILURE;
    } else {
        for(auto& word:stopword_lines){
            stop_words_set.insert(word);
        }
        out << "StopWordsCount: " << stop_words_set.size() << "\n";
    }

    //scan the input file line by line
   
    if (!ReadUtf8Lines(inputpath, lines)) {
        std::cerr << "[ERROR] cannot open input file: " << inputpath << std::endl;
        return EXIT_FAILURE;
    } else if(lines.size() == 0){
        std::cout << "[INFO] input file is empty: " << inputpath << std::endl;
        return EXIT_FAILURE;
    } else  {
        std::cout << "[INFO] read " << lines.size() << " lines from " << inputpath << std::endl;
        out << "LineCount: " << lines.size() << "\n";
    }
    ll out_time = 0;
    auto cmp = [&](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
        //compare count first, than letter order
        if(a.second == b.second) return a.first > b.first; // max-heap
        return a.second < b.second; // min-heap based on count
    };
    for( size_t idx = 0; idx < lines.size(); ++idx ) {
        const std::string& contents = lines[idx];
        std::string action_str = extractAction(contents);
        ll currtime;
        int h, m, s;
        ll queryTime=-1;
        if (!checkTime(action_str, h, m, s)){
            std::string require = extractSentence(contents);
            queryTime = check_start_time(require);
                if (queryTime == -1){
                out<< "[WARNING] Line " << idx+1 << ": cannot extract valid time info from action string: " << action_str << "\n";
                continue;
            }
        }
        //add to real_time_word_map
        if (queryTime == -1){
            currtime = h * 3600 + m * 60 + s;
            ll old_time = currtime - cfg.time_range * 60 >= 0 ? currtime - cfg.time_range * 60 : 0;
            if (out_time != old_time){
                //remove outdated records
                auto it = real_time_word_map.begin();
                while(it->first < out_time && it != real_time_word_map.end()){
                    real_time_word_map.erase(it++);
                    word_count_map[it->second]--;
                }
            }
            //add current records
            std::string sentence = extractSentence(contents);
            std::vector<std::pair<std::string, std::string>> tagres;
            jieba.Tag(sentence, tagres);
            for(auto& v:tagres){
                if(stop_words_set.find(v.first) != stop_words_set.end()) continue;
                word_tag_map[v.first] = v.second;
                time_word_map.insert(std::make_pair(currtime, v.first));
                real_time_word_map.insert(std::make_pair(currtime, v.first));
                //counter++
                if (word_count_map.find(v.first) == word_count_map.end()){
                    word_count_map[v.first] = 1;
                } else {
                    word_count_map[v.first] += 1;
                }
            }
        } else {
            ll qtime = queryTime*60;
            out << "Query Time: " << queryTime << " minute" << "\n";
            if(qtime == currtime){
                //output hot words for this time
                std::priority_queue<std::pair<std::string, int>, std::vector<std::pair<std::string, int>>, decltype(cmp)> pq(cmp);
                for(auto& p:word_count_map){
                    pq.push(std::make_pair(p.first, p.second));
                }
                for(size_t i = 1; i <= cfg.topk; ++i){
                        if(!pq.empty()){
                            auto top = pq.top();
                            pq.pop();
                            out << i << ": " << top.first << "/" << word_tag_map[top.first] << "/" << top.second << std::endl;
                        }
                }
            }else{
                //any time query
                ll stime = qtime - cfg.time_range * 60>=0? qtime - cfg.time_range * 60 : 0;
                std::unordered_map <std::string, int> w_c_m;
                std::priority_queue<std::pair<std::string, int>, std::vector<std::pair<std::string, int>>, decltype(cmp)> pq(cmp);
                auto it = time_word_map.find(stime);
                while(it!= time_word_map.end() && it->first <= qtime){
                    if(w_c_m.find(it->second) == w_c_m.end()){
                        w_c_m[it->second] = 1;
                    } else {
                        w_c_m[it->second] += 1;
                    }
                    it++;
                }
                for(auto& p:w_c_m){
                    pq.push(std::make_pair(p.first, p.second));
                }
                for(size_t i = 1; i <= cfg.topk; ++i){
                        if(!pq.empty()){
                            auto top = pq.top();
                            pq.pop();
                            out << i << ": " << top.first << "/" << word_tag_map[top.first] << "/" << top.second << std::endl;
                        }
                }
            }
        
        }
   
    }
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

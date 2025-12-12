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
    std::unordered_multimap<long long, std::string> time_word_map;
    std::unordered_map <std::string, std::string> word_tag_map;
    std::unordered_map <std::string, int> word_count_map;
    std::unordered_set <std::string> stop_words_set;

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
    
    for( size_t idx = 0; idx < lines.size(); ++idx ) {
        const std::string& contents = lines[idx];
        std::string time_str = extractTime(contents);
        long long time = calculateTime(time_str);
        std::string sentence = extractSentence(contents);
        std::vector<std::pair<std::string, std::string>> tagres;
        jieba.Tag(sentence, tagres);//tagres: <word, tag>
        for(auto& v:tagres){
            if(stop_words_set.find(v.first) != stop_words_set.end()) continue;
            word_tag_map[v.first] = v.second;
            time_word_map.insert(std::make_pair(time, v.first));
        }
    }
    std::cout<<"successfully process "<< lines.size() <<" lines from input file."<<std::endl;
    while(true){
        std::cout<<"Choosing the options:\n"<<"1: Searching Topk hot words with time-based sliding window\n"
        <<"2: quit\n";
        int option;
        std::cin>>option;
        if (option==2) {
            break;
        } else if (option==1){
            out<< "===== Time-based Sliding Window TopK Hot Words =====\n";
            auto cmp = [&](std::pair<std::string, int>& a, std::pair<std::string, int>& b){
                return a.second < b.second; //max-heap
            };
            std::priority_queue<std::pair<std::string, int>, std::vector<std::pair<std::string, int>>, decltype(cmp)> pq(cmp);
            std::string start_time_str;
            if (char c = std::getc(stdin) != '\n')
                std::ungetc(c, stdin); //clear the buffer
            std::cout<<"Please input the start time point (HH:MM:SS): ";
            std::getline(std::cin, start_time_str); //读掉但没放进字符串，太好用了！
            int h, m, s;
            sscanf(start_time_str.c_str(), "%*[^0-9]%d:%d:%d", &h, &m, &s);//skip non-digit characters
            int start_time = h * 3600 + m * 60 + s;

            long long time_range = cfg.time_range;
            long long time_range_s = time_range*60;
            long long end_time = start_time + time_range_s;
            int topk = cfg.topk;
            word_count_map.clear();

            for(auto& p:time_word_map){
                if(p.first >= start_time && p.first <= end_time){
                    if(word_count_map.find(p.second) == word_count_map.end()){
                        word_count_map[p.second] = 1;
                    } else {
                        word_count_map[p.second] += 1;
                    }
                }
            }

            for(auto& wc:word_count_map){
                pq.push(std::make_pair(wc.first, wc.second));
            }

            out << "StartTime: " << start_time_str << "\n";
            out << "TimeRange(min): " << time_range << "\n";
            for(int i=0; i<topk && !pq.empty(); ++i){
                auto p = pq.top();
                pq.pop();
                out << i+1 << ":" << p.first << "/" << word_tag_map[p.first] << "/" << p.second << "\n";
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
    
    std::string userterms = std::string(JIEBA_DICT_DIR) + "/user_word.txt";

    cppjieba::Jieba jieba(mainDict, hmmModel, userDict, idfFile, stopFile);
    
    std::vector<std::string> userterms_vec;
    ReadUtf8Lines(userterms, userterms_vec);
    for(auto &word:userterms_vec){
        jieba.InsertUserWord(word);
    }
    //add user words(proper nouns) to improve the completeness of recognition

    if(work_type==1){
        deal_with_file_input(jieba, cfg);
    } 
    return 0;
    
}

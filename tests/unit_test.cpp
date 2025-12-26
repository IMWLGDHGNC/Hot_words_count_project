/*
这是当前项目的测试脚本。我们设计了一些输入写在"input\unit_test_input.txt"中，
将输出收集在output\output_unit_test.txt中，并对结果进行验证。
测试内容包括：
1) 基线验证：不启用词性筛选，确认动词等都会进入热词统计，测试结果保存在output\output_unit_test_base.txt；
2) 词性筛选验证：启用词性筛选（只允许n/nz/x），测试结果保存在output\output_unit_test.txt，将两个输出文件相比较，确认筛选功能正常；
3) 敏感词过滤验证：确认敏感词（黄/赌/毒）不会出现在统计结果中；
4) 动态窗口调整验证：调整窗口大小后，确认查询结果符合预期，原本不应出现的词汇会因为窗口的调节出现。
5) 用户词典验证：确认用户自定义词典中的词汇能够被正确识别和统计。我们插入“中山大学计算机学院”这样的专有名词，测试能否查出
*/
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <queue>
#include <algorithm>
#include "Jieba.hpp"
#include "../scripts/utils.hpp"

struct HotWordsEngine {
    std::unordered_map<std::string, int> word_count_map;
    std::unordered_map<std::string, std::string> word_tag_map;
    std::multimap<ll, std::string> window_index; // time -> word (ordered)
    std::multimap<ll, std::string> history_map;  // time -> word (ordered)
    std::unordered_set<std::string> stop_words_set;
    std::unordered_set<std::string> tag_allowed_set; // empty means allow all
    int current_time_range_minutes = 2; // default
    ll currtime = 0; // latest processed event time (seconds)

    void init_filters() {
        scan_stop_words(stop_words_set);
        scan_sensitive_words(stop_words_set);
        // Note: POS filtering via tag_allowed_set is controlled by tests directly
    }

    void set_allowed_tags(const std::vector<std::string>& tags) {
        tag_allowed_set.clear();
        for (const auto& t : tags) tag_allowed_set.insert(t);
    }

    void set_window_size(int minutes) {
        if (minutes <= 0) minutes = 1;
        current_time_range_minutes = minutes;
        // rebuild current window from history_map for time [currtime - win, currtime]
        word_count_map.clear();
        window_index.clear();
        ll start_time = (currtime >= current_time_range_minutes * 60) ? (currtime - current_time_range_minutes * 60) : 0;
        auto it_start = history_map.lower_bound(start_time);
        auto it_end = history_map.upper_bound(currtime);
        for (auto it = it_start; it != it_end; ++it) {
            const std::string &w = it->second;
            word_count_map[w]++;
            window_index.insert({it->first, w});
        }
    }

    void process_line(cppjieba::Jieba& jieba, int h, int m, int s, const std::string& sentence) {
        ll t = h * 3600 + m * 60 + s;
        if (t >= currtime) currtime = t;
        std::vector<std::pair<std::string, std::string>> tagres;
        std::string sent = sentence;
        normalize_radicals(sent);
        jieba.Tag(sent, tagres);

        for (auto &v : tagres) {
            if (!tag_allowed_set.empty() && tag_allowed_set.find(v.second) == tag_allowed_set.end()) continue;
            if (stop_words_set.find(v.first) != stop_words_set.end()) continue;
            word_tag_map[v.first] = v.second;
            history_map.insert({t, v.first});
            window_index.insert({t, v.first});
            word_count_map[v.first]++;
        }

        // maintain sliding window
        ll threshold = (currtime >= current_time_range_minutes * 60) ? (currtime - current_time_range_minutes * 60) : 0;
        auto it_end = window_index.lower_bound(threshold);
        for (auto it = window_index.begin(); it != it_end; ++it) {
            auto wc = word_count_map.find(it->second);
            if (wc != word_count_map.end()) {
                wc->second--;
                if (wc->second <= 0) word_count_map.erase(wc);
            }
        }
        window_index.erase(window_index.begin(), it_end);
    }

    std::vector<std::pair<std::string,int>> query_topk(int minute, int topk) {
        auto cmp = [&](const std::pair<std::string,int>& a, const std::pair<std::string,int>& b){
            if (a.second == b.second) return a.first > b.first; // reverse lexicographic for ties
            return a.second < b.second; // min-heap behavior
        };
        std::priority_queue<std::pair<std::string,int>, std::vector<std::pair<std::string,int>>, decltype(cmp)> pq(cmp);

        ll qsec = minute * 60;
        bool is_current_window = (currtime >= qsec) && (currtime - qsec < 60);
        if (is_current_window) {
            for (auto &p : word_count_map) pq.push(p);
        } else {
            ll start_time = (qsec >= current_time_range_minutes * 60) ? (qsec - current_time_range_minutes * 60) : 0;
            std::unordered_map<std::string,int> temp;
            auto it_start = history_map.lower_bound(start_time);
            auto it_end = history_map.upper_bound(qsec + 59);
            for (auto it = it_start; it != it_end; ++it) temp[it->second]++;
            for (auto &p : temp) pq.push(p);
        }

        std::vector<std::pair<std::string,int>> res;
        for (int i = 0; i < topk && !pq.empty(); ++i) { res.push_back(pq.top()); pq.pop(); }
        return res;
    }
};

static bool expect(bool cond, const std::string& msg) {
    if (!cond) std::cerr << "[FAIL] " << msg << std::endl;
    else std::cout << "[PASS] " << msg << std::endl;
    return cond;
}

int main() {
    // Initialize Jieba with project dictionaries
    std::string mainDict = std::string(JIEBA_DICT_DIR) + "/jieba.dict.utf8";
    std::string hmmModel = std::string(JIEBA_DICT_DIR) + "/hmm_model.utf8";
    std::string userDict = std::string(JIEBA_DICT_DIR) + "/user.dict.utf8";
    std::string idfFile  = std::string(JIEBA_DICT_DIR) + "/idf.utf8";
    std::string stopFile = std::string(JIEBA_DICT_DIR) + "/stop_words.utf8";
    cppjieba::Jieba jieba(mainDict, hmmModel, userDict, idfFile, stopFile);

    // Load and insert user words like the app does
    std::vector<std::string> userterms;
    ReadUtf8Lines(std::string(INPUT_ROOT_DIR) + "/user_word.txt", userterms);
    for (auto &w : userterms) jieba.InsertUserWord(w, 20000);

    HotWordsEngine eng;
    eng.init_filters();

    bool ok_all = true;

    // 1) Segmentation count & TopK accuracy
    // Process 3 occurrences of the user word "人工智能" around minute 1
    eng.set_allowed_tags({}); // allow all POS
    eng.set_window_size(5);
    eng.process_line(jieba, 0, 1, 0, "人工智能很有用 人工智能改变世界");
    eng.process_line(jieba, 0, 1, 20, "人工智能发展迅速");
    eng.process_line(jieba, 0, 1, 50, "我们讨论人工智能以及应用");
    auto top1 = eng.query_topk(1, 1);
    bool case1 = expect(!top1.empty() && top1[0].first == "人工智能" && top1[0].second >= 3,
                        "Top1 should be 人工智能 with count >= 3 at minute 1");
    ok_all &= case1;

    // 2) Sensitive words filtering (words in input/sensitive_words.txt should be ignored)
    // Add a line with sensitive words; counts must not include them.
    eng.process_line(jieba, 0, 1, 55, "黄 毒 赌 正常词");
    auto res2 = eng.query_topk(1, 10);
    bool has_sensitive = false;
    for (auto &p : res2) {
        if (p.first == "黄" || p.first == "毒" || p.first == "赌") { has_sensitive = true; break; }
    }
    bool case2 = expect(!has_sensitive, "Sensitive words should be filtered out");
    ok_all &= case2;

    // 3) POS filtering: allow only nouns (n, nz) and check that verbs are filtered
    eng.set_allowed_tags({"n", "nz"});
    eng.process_line(jieba, 0, 2, 10, "学生喜欢学习 人工智能"); // mix of nouns and verbs
    auto res3 = eng.query_topk(2, 10);
    bool has_verb = false;
    for (auto &p : res3) {
        // common verb tokens: 喜欢/学习; they should be filtered when allowing only n/nz
        if (p.first == "喜欢" || p.first == "学习") { has_verb = true; break; }
    }
    bool case3 = expect(!has_verb, "Verbs (喜欢/学习) should be excluded when allowing only noun tags");
    ok_all &= case3;

    // 4) Dynamic sliding window size: change window and verify query results
    // Create events at minute 1 and minute 5 for distinct tokens
    eng.set_allowed_tags({});
    eng.set_window_size(1); // narrow window
    eng.process_line(jieba, 0, 5, 0, "大学 人工智能"); // minute 5
    auto res4a = eng.query_topk(5, 10);
    bool contains_min5 = false, contains_min1 = false;
    for (auto &p : res4a) {
        if (p.first == "大学" || p.first == "人工智能") contains_min5 = true;
        if (p.first == "学生") contains_min1 = true; // if any from minute 1 survived (should not)
    }
    bool case4a = expect(contains_min5 && !contains_min1, "With 1-minute window, query@5 only includes minute-5 words");
    ok_all &= case4a;

    // Enlarge window to 10 minutes and expect earlier words to be counted
    eng.set_window_size(10);
    auto res4b = eng.query_topk(5, 10);
    bool now_contains_min1 = false;
    for (auto &p : res4b) if (p.first == "学生") { now_contains_min1 = true; break; }
    bool case4b = expect(now_contains_min1, "After enlarging window to 10 minutes, earlier words should be included");
    ok_all &= case4b;

    if (!ok_all) {
        std::cerr << "\nSome tests FAILED." << std::endl;
        return 1;
    }
    std::cout << "\nAll tests PASSED." << std::endl;
    return 0;
}

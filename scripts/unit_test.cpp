// 本脚本做两轮验证：
// 1) 基线：不启用词性筛选，确认动词等都会进入热词统计；
// 2) 词性筛选：写入 tag.txt（n/nz），再次跑流程，确认动词被过滤，只保留名词；
// 末尾将单测总结与两轮查询结果追加到 output/output_unit_test.txt，便于对比。

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include "Jieba.hpp"
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

// Local re-declarations to avoid including utils.hpp (which defines non-inline functions)
// Keep in sync with scripts/utils.hpp
struct Config {
    std::string inputFile;
    std::string outputFile;
    std::string dictDir;
    std::string jiebamode;
    int topk;
    int time_range;
    int work_type;
};

// Forward declarations of functions defined in scripts/main.cpp
int deal_with_file_input(cppjieba::Jieba& jieba, const Config& cfg);
int deal_with_console_input(cppjieba::Jieba& jieba, const Config& cfg);

static std::vector<std::string> g_logs; // collect PASS/FAIL lines for appending to output file

static bool expect(bool cond, const std::string& msg) {
    const std::string line = std::string(cond ? "[PASS] " : "[FAIL] ") + msg;
    g_logs.push_back(line);
    if (!cond) std::cerr << line << std::endl;
    else std::cout << line << std::endl;
    return cond;
}

// Helper: read all lines from a file
static std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return lines;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

int main() {
    // 确保正确的输入输出
    #ifdef _WIN32
    SetConsoleOutputCP(65001);//setting the output to utf-8
    SetConsoleCP(65001);//setting the input to utf-8
    // Ensure stdout and stdin are unbuffered for interactive mode
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stdin, nullptr, _IONBF, 0);
    #endif
    // Prepare config to use file-input mode with our synthetic test file
    Config cfg{};
    cfg.inputFile = "unit_test_input.txt";
    cfg.outputFile = "output_unit_test.txt";
    cfg.dictDir = "dict";
    cfg.jiebamode = "tagres";
    cfg.topk = 5;
    cfg.time_range = 2; // default; will be modified inside stream via WINDOW_SIZE
    cfg.work_type = 1;  // file input mode

    // 统一构造测试输入：含多次“人工智能”、敏感词、以及包含动词的句子用于词性对比
    std::string inpath = std::string(INPUT_ROOT_DIR) + "/" + cfg.inputFile;
    {
        std::ofstream out(inpath, std::ios::binary);
        out << "[00:01:00] 人工智能很有用 人工智能改变世界\n";
        out << "[00:01:20] 人工智能发展迅速\n";
        out << "[00:01:50] 我们讨论人工智能以及应用\n";
        out << "[00:01:55] 黄 毒 赌 正常词\n"; // 敏感词应被屏蔽
        out << "[ACTION] QUERY K=1\n";
        out << "[00:02:10] 学生 喜欢 学习 人工智能\n"; // 含动词，方便对比词性筛选
        out << "[ACTION] QUERY K=2\n"; // 针对 2 分钟做查询，对比词性筛选前后差异
        out << "[ACTION] WINDOW_SIZE=1\n";
        out << "[00:05:00] 大学 人工智能\n";
        out << "[00:05:30] 大学 大学 大学\n";
        out << "[ACTION] QUERY K=5\n";
        out << "[ACTION] WINDOW_SIZE=10\n";
        out << "[ACTION] QUERY K=5\n";

        // 加入专有名词，检测用户词是否可被查询到
        out << "[00:03:10] 中山大学计算机学院 中山大学计算机学院\n";
        out << "[ACTION] QUERY K=3\n";
    }

    // Initialize Jieba with project dictionaries (same as app)
    std::string mainDict = std::string(JIEBA_DICT_DIR) + "/jieba.dict.utf8";
    std::string hmmModel = std::string(JIEBA_DICT_DIR) + "/hmm_model.utf8";
    std::string userDict = std::string(JIEBA_DICT_DIR) + "/user.dict.utf8";
    std::string idfFile  = std::string(JIEBA_DICT_DIR) + "/idf.utf8";
    std::string stopFile = std::string(JIEBA_DICT_DIR) + "/stop_words.utf8";
    cppjieba::Jieba jieba(mainDict, hmmModel, userDict, idfFile, stopFile);

    // Insert user words (e.g., 人工智能) to match app behavior
    // 插入专有名词之后，再进行测试
    std::vector<std::string> userterms;
    {
        std::ifstream uf(std::string(INPUT_ROOT_DIR) + "/user_word.txt", std::ios::binary);
        std::string w;
        while (std::getline(uf, w)) {
            if (!w.empty() && w.back() == '\r') w.pop_back();
            if (!w.empty()) {
                userterms.push_back(w);
                jieba.InsertUserWord(w, 20000);
            }
        }
    }

    auto write_tag = [](const std::vector<std::string>& tags){
        std::ofstream tagf(std::string(INPUT_ROOT_DIR) + "/tag.txt", std::ios::binary);
        for (auto &t : tags) tagf << t << "\n";
    };

    auto run_once = [&](const std::vector<std::string>& tags, const std::string& outFile){
        write_tag(tags);
        Config local = cfg;
        local.outputFile = outFile;
        int ret = deal_with_file_input(jieba, local);
        if (ret != EXIT_SUCCESS) {
            std::cerr << "Pipeline failed with code: " << ret << std::endl;
            return std::vector<std::string>{};
        }
        return read_lines(std::string(OUTPUT_ROOT_DIR) + "/" + outFile);
    };

    // 先跑一遍（无词性筛选），再跑一遍（n/nz词性筛选）
    auto lines_base = run_once({}, "output_unit_test_base.txt");
    auto lines = run_once({"n", "nz", "x"}, cfg.outputFile);

    // Utility lambdas to parse query blocks
    auto find_query_index = [&](int minute){
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find("Query Time: ") != std::string::npos) {
                // Example: "Query Time: 1 minute"
                std::string s = lines[i];
                size_t p = s.find(": ");
                size_t q = s.find(" minute");
                if (p != std::string::npos && q != std::string::npos) {
                    int m = std::atoi(s.substr(p + 2, q - (p + 2)).c_str());
                    if (m == minute) return (int)i;
                }
            }
        }
        return -1;
    };

    auto collect_top_lines = [&](const std::vector<std::string>& src, int start_idx){
        if (start_idx < 0) return std::vector<std::string>{};
        std::vector<std::string> res;
        for (size_t i = start_idx + 1; i < src.size(); ++i) {
            if (src[i].rfind("[INFO]", 0) == 0 || src[i].find("Query Time:") != std::string::npos || src[i].find("Program Metrics") != std::string::npos) break;
            if (!src[i].empty() && isdigit(static_cast<unsigned char>(src[i][0]))) res.push_back(src[i]);
        }
        return res;
    };

    auto contains_word = [&](const std::vector<std::string>& rows, const std::string& w){
        for (auto &r : rows) if (r.find(w + "/") != std::string::npos) return true; // format: "i: word/tag/count"
        return false;
    };

    // 1) 基线 vs 词性筛选：分钟2查询时，基线应包含动词喜欢/学习，筛选后应剔除
    auto q2_base = [&]{
        int idx = -1; for (size_t i = 0; i < lines_base.size(); ++i) if (lines_base[i].find("Query Time: 2 minute") != std::string::npos) { idx = (int)i; break; }
        return collect_top_lines(lines_base, idx);
    }();
    int q2 = find_query_index(2);
    auto top2_rows = collect_top_lines(lines, q2);
    bool base_has_verbs = contains_word(q2_base, "喜欢") || contains_word(q2_base, "学习");
    bool filtered_has_verbs = contains_word(top2_rows, "喜欢") || contains_word(top2_rows, "学习");
    bool case_pos_diff = expect(base_has_verbs && !filtered_has_verbs, "词性筛选生效：基线含动词，筛选后剔除喜欢/学习");

    // 2) Top-k 结果不为空且为名词（在开启词性筛选后）
    int q1 = find_query_index(1);
    auto top1_rows = collect_top_lines(lines, q1);
    bool case1 = expect(!top1_rows.empty(), "TopK@1 不应为空");
    bool case1b = true;
    for (auto &r : top1_rows) { if (r.find("/n/") == std::string::npos && r.find("/nz/") == std::string::npos && r.find("/x/") == std::string::npos) { case1b = false; break; } }
    case1b = expect(case1b, "TopK@1 词性筛选后应只包含名词或专有名词 (x)");

    // 3) 敏感词过滤：黄/赌/毒 不应出现
    bool case2 = expect(!contains_word(top1_rows, "黄") && !contains_word(top1_rows, "赌") && !contains_word(top1_rows, "毒"),
                        "敏感词应被过滤 (黄/赌/毒)");

    // 4) 动态滑窗：1 分钟窗口只含近期词，扩到 10 分钟后补回早期词
    int q5_first = find_query_index(5); // after WINDOW_SIZE=1
    auto top5_first = collect_top_lines(lines, q5_first);
    bool case4a = expect(contains_word(top5_first, "大学") && !contains_word(top5_first, "学生"),
                         "1 分钟窗口下，查询@5 仅含近期词 (大学) 不含早期词 (学生)");
    int q5_second = -1;
    for (size_t i = q5_first + 1; i < lines.size(); ++i) {
        if (lines[i].find("Query Time: 5 minute") != std::string::npos) { q5_second = (int)i; break; }
    }
    auto top5_second = collect_top_lines(lines, q5_second);
    bool case4b = expect(contains_word(top5_second, "学生"),
                         "10 分钟窗口下，查询@5 应补回早期词 (学生)");

    // 5) 用户词（专有名词）：基线查询@3 应能查出“中山大学计算机学院”
    auto q3_base = [&]{
        int idx = -1; for (size_t i = 0; i < lines_base.size(); ++i) if (lines_base[i].find("Query Time: 3 minute") != std::string::npos) { idx = (int)i; break; }
        return collect_top_lines(lines_base, idx);
    }();
    bool case_user = expect(contains_word(q3_base, "中山大学计算机学院"), "用户词可查询：基线 Query@3 包含 中山大学计算机学院");
    // 词性筛选（含 x）下，Query@3 也应包含该专有名词
    auto q3_filtered = [&]{
        int idx = -1; for (size_t i = 0; i < lines.size(); ++i) if (lines[i].find("Query Time: 3 minute") != std::string::npos) { idx = (int)i; break; }
        return collect_top_lines(lines, idx);
    }();
    bool case_user_filtered = expect(contains_word(q3_filtered, "中山大学计算机学院"), "用户词可查询：筛选 (含 x) Query@3 包含 中山大学计算机学院");

    auto append_logs = [&](bool all_ok){
        std::ofstream ofs(std::string(OUTPUT_ROOT_DIR) + "/" + cfg.outputFile, std::ios::binary | std::ios::app);
        if (!ofs.is_open()) return;
        ofs << "\n===== UnitTest Summary =====\n";
        for (auto &l : g_logs) ofs << l << "\n";
        ofs << (all_ok ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
        ofs << "\n===== 基线 (无词性筛选) 查询@2 =====\n";
        for (auto &l : q2_base) ofs << l << "\n";
        ofs << "\n===== 词性筛选 (n/nz) 查询@2 =====\n";
        for (auto &l : top2_rows) ofs << l << "\n";
        ofs << "\n===== 基线 (无词性筛选) 查询@3 =====\n";
        for (auto &l : q3_base) ofs << l << "\n";
        ofs << "\n===== 词性筛选 (n/nz/x) 查询@3 =====\n";
        for (auto &l : q3_filtered) ofs << l << "\n";
    };

    if (!(case1 && case1b && case2 && case4b && case4a && case_pos_diff && case_user && case_user_filtered)) {
        std::cerr << "\nSome tests FAILED." << std::endl;
        append_logs(false);
        return 1;
    }
    std::cout << "\nAll tests PASSED." << std::endl;
    append_logs(true);
    return 0;
}

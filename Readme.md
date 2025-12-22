# Hot_words_count 实时热词统计与可视化

本项目是一个基于 C++ 与 cppjieba 的中文热词统计系统，支持文件/交互式两种输入模式，并提供 Flask Web 可视化界面。系统面向“实时流式文本”的热词分析场景，支持时间窗口管理、Top-K 维护、敏感/停用词过滤、词性筛选等功能。

---

## 功能概述
- **热词统计**: 利用"jieba"库，从中文文本中分词与词性标注，过滤停用/敏感词与非允许词性，维护滑动时间窗口内的词频，并支持任意给定“分钟”时刻的 Top-K 查询。
- **实时交互**: 控制台模式可动态输入文本与指令，实时调整窗口大小并查询热点词。
- **可视化 Web**: 提供简易 WebUI（Flask）用于配置、运行、查看输出与解析 Top-K 快照。

---

## 核心实现

#### 1. 实时计数器

- **数据结构**: `std::unordered_map<std::string,int>` 维护当前窗口内各词频次（`word_count_map`）。
- **更新逻辑**: 对于输入的新词，判断其时间是否处在当前窗口内，如果是则该词计数自增即可。
- **时间复杂度**: 单词计数更新为均摊 $O(1)$；插入到有序索引为 $O(\log M)$（`multimap`，$M$ 为窗口内总条目）。

#### 2. 时间窗口管理器
- **数据结构**: 两个时间有序索引 `std::multimap<ll,std::string>`：
	- `window_index` 存储当前活动窗口的时间→词条，用于按阈值批量淘汰过期数据。
	- `history_map` 存储全量历史时间→词条，用来支持对过去任意分钟的查询。
- **过期淘汰**: 计算阈值时间 `threshold_time`，对 `window_index.lower_bound(threshold_time)` 之前的条目逐个将计数减一并移除；当计数归零时从 `word_count_map` 擦除。
- **窗口调整**: 控制台/文件输入均支持动态指令 `WINDOW_SIZE = N`（分钟），用来修改查询的时间窗口大小，即时生效。
- **时间复杂度**: 淘汰阶段对每个过期条目为 $O(\log M)$（定位）+ 均摊 $O(1)$（计数更新），批量为过期条目数的线性成本。
- **设计取舍**：对于全局时间->词条映射history_map，原本使用的是双端队列deque，为了实现**迟到数据插入以及任意时间查询**，换成了multimap数据结构。

#### **Top-K 维护结构**
- **数据结构**: 使用 `std::priority_queue<std::pair<std::string,int>>`（最大堆）与自定义比较器。
- 根据所需查询的时间，分为两种情况：
    - **当前分钟查询**: 直接将 `word_count_map` 全量推入堆，再弹出前 `K`；
    - **历史分钟查询**: 基于 `history_map` 在 `[start, end]` 区间重构临时 `unordered_map`，再推入堆求 Top-K。
- **时间复杂度**: 推入 $n$ 个词的堆构建为 $O(n\log n)$，弹出前 $K$ 为 $O(K\log n)$。如果是历史时间查询，还需要重新构建word_count_map，复杂度同实时计数器。

---

## **性能测试**
- **度量指标**: 程序在输出文件末尾写入三项指标：
	- 吞吐: `Throughput(lines/sec)` 表示每秒处理的输入行数。
	- 延迟: `AvgLatency(ms/line)` 表示每行的平均处理时延（毫秒）。
	- 内存: `Memory(MB)` 运行时工作集内存（Windows）。
- **采集方式**: 文件模式按整批输入统计，交互式模式按累计处理行统计。指标可在输出文件中查看，位置见“运行与使用”。
- **结果说明**: 指标与语料规模、词典大小、允许词性筛选与窗口大小相关。请使用自己的数据集在相同环境下复现与记录结果。

---

## **目录结构**
- 源码与脚本
	- [scripts/main.cpp](scripts/main.cpp): 主程序（文件/交互两模式、窗口与查询逻辑）。
	- [scripts/utils.hpp](scripts/utils.hpp): 配置加载、分词辅助、指令解析、工具函数。
	- [demo.cpp](demo.cpp): 可选演示入口（通过 `BUILD_DEMO` 打开）。
- 词典与第三方
	- [dict/](dict): `jieba.dict.utf8`、`hmm_model.utf8`、`idf.utf8`、`stop_words.utf8` 等资源。
	- [third_party/utfcpp/](third_party/utfcpp): UTF 编解码库（用于偏旁归一等）。
	- [cppjieba/](cppjieba): cppjieba 头文件与依赖。
- 数据与输出
	- [input/](input): 示例输入与配置相关文件（如 `test_sentences.txt`、`sensitive_words.txt`、`tag.txt`、`user_word.txt`）。
	- [output/](output): 运行输出（如 `output.txt`）。
- 构建与 Web
	- [CMakeLists.txt](CMakeLists.txt): CMake 工程配置，生成 `hotwords` 可执行文件。
	- [webui/app.py](webui/app.py): Flask WebUI。
	- [webui/requirements.txt](webui/requirements.txt): Web 依赖。

---

## **构建指南**
**前置依赖**

	- C++17 编译器（Windows 推荐 MSYS2 UCRT64 `g++`）
	- CMake ≥ 3.16
	- python3
**编译步骤**

1. 克隆项目
```
git clone https://github.com/IMWLGDHGNC/Hot_words_count_project.git
cd Hot_words_count_project
```
2. 创建目录并运行cmake配置
```
mkdir build
cd build
cmake ..
```
3. 编译项目
```
make -j$(nproc) #Linux/macOS
cmake --build . #windows
```
---

## 运行与使用
#### 前期设置
- **输入部分**
    - sensitive_words.txt: 输入需要屏蔽的敏感词。
    - tag.txt: 输入筛选的词性，参考[jieba词性表]。(https://blog.csdn.net/Yellow_python/article/details/83991967)，如:'n'表示只选出名词；如果文档为空，则默认为不筛选词性。
    - user_word.txt: 用户输入的专业名词，如“中山大学计算机学院”。
    - 所有的输入文件都放在该目录下。
- **输出部分**
    - output.txt: 系统输出文档。
- **配置文件**: [config.ini](config.ini)
    1.input_file: 文件输入下的输入文件，务必确保该文件在...\input下。
    2.output_file: 系统处理的输出文件名，务必确保该文件在...\output下。
    3.dict_dir: jieba库自带的字典名。
    4.mode: jieba分词模式，必须开启tagres模式以实现词性筛选。
    5.topk: 热词统计范围。
    6.time_range: 时间窗口大小。
    7.work_type: 1:文件输入模式 2：终端输入模式
    8.normalize: 是否对非标准utf-8输入的中文进行标准化

- **文件模式**（离线批处理）
	1. 设置 [config.ini](config.ini) 中的 `input_file` 与 `output_file`。
	2. 运行二进制：
		 .\build\hotwords.exe
		 ```
	3. 在 [output/output.txt](output/output.txt) 末尾查看性能指标与查询结果快照。

- **交互模式（实时输入）**
	1. 将 `work_type = 2`。
	2. 运行二进制后，按提示格式输入：
		 - `[HH:MM:SS] 句子内容`：显式时间事件。
		 - `句子内容`：隐式使用当前时间。
		 - `[ACTION] QUERY K=15`：查询第 15 分钟 Top-K（`topk` 由配置决定）。
		 - `[ACTION] WINDOW_SIZE=10`：将滑动窗口调整为 10 分钟。
	3. 输入 `exit` 退出；输出写至 [output/output.txt](output/output.txt)。

- **Web 可视化（Flask）**
	1. 安装依赖：
		 ```
		 python -m pip install -r webui/requirements.txt
		 ```
	2. 启动服务：
		 ```
		 python webui/app.py
		 ```
         打开您的浏览器并访问：
         (http://127.0.0.1:8080/)[http://127.0.0.1:8080/]

	<!-- 3. 功能：
		 - 查看/更新配置（除 `dict_dir`、`mode` 由后端控制）。
		 - 执行批处理运行并预览输出。
		 - 上传输入文件（保存至 [input/](input)，自动更新 `input_file`）。
		 - 交互式控制台：启动/输入/查看输出（强制 `work_type=2`）。
		 - 解析输出中的查询快照列表 `/api/output_parsed`。 -->

---

## 输入指令说明（交互模式）
- 时间事件: `[HH:MM:SS] sentence`
	- 示例: `[12:34:56] 人工智能正在改变世界`
- 即时事件（无时间戳）: `sentence`
	- 示例: `机器学习与深度学习`
- 查询 Top-K: `[ACTION] QUERY K=15`
	- 解释: 查询第 15 分钟窗口的热点词；若为当前分钟，直接基于 `word_count_map`，否则从 `history_map` 重构区间统计。
- 调整窗口: `[ACTION] WINDOW_SIZE=10`
	- 解释: 将滑动窗口大小调整为 10 分钟，后续过期淘汰与查询均按新窗口执行。

---

## 设计与复杂度小结
- 分词与词性标注: 由 cppjieba 完成（复杂度与句长相关，近似线性）。
- 数据维护: 插入与淘汰基于 `multimap` 的有序时间索引，支持迟到与历史查询。
- Top-K 查询: 当前实现为“全量推堆+弹出 K”，复杂度 $O(n\log n + K\log n)$；可按需改为 $O(n\log K)$ 的最小堆优化。
---

## References
- [cppjieba/](cppjieba) 与其词典资源 [dict/](dict)
- [utfcpp](third_party/utfcpp) 提供 UTF 编解码支持

如需进一步功能（如持久化、并发输入、REST API 热词查询等），欢迎在此基础上扩展。
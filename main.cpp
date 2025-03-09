#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>

using namespace std;

// ANSI颜色代码
#define COLOR_RESET   "\033[0m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"

// 配置参数
const double MIN_GAIN = 1.0;
const double MAX_GAIN = 100.0;
const double EPSILON = 1e-6;

struct Config {
    double x1, x2;
    double Vmin, Vmax;
    double step = 1.0;
    int threads = thread::hardware_concurrency();

    bool validate() const {
        return (x1 < x2) && (Vmin < Vmax) &&
               (step > EPSILON) && (threads > 0);
    }
};

struct Solution {
    double O1min, O1max;
    double O2min, O2max;
    vector<double> gains;
    vector<double> thresholds;
};

// 全局共享数据
mutex solutions_mutex;
vector<Solution> global_solutions;
atomic<int> processed_counter(0);
double total_iterations = 0;

void print_help() {
    cout << COLOR_YELLOW
         << "Two-stage Amplifier Optimizer (Parallel v2.2)\n"
         << COLOR_RESET "Usage: ./amplifier -i <in_min> <in_max> -o <out_min> <out_max> [options]\n\n"
         << "Required Parameters:\n"
         << "  -i    Input voltage range (e.g. -i 0.03 0.6)\n"
         << "  -o    Output voltage range (e.g. -o 1 1.9)\n\n"
         << "Options:\n"
         << "  -s    Gain step size (default: 1.0)\n"
         << "  -j    Thread number (default: CPU cores)\n"
         << "  -h    Show this help\n\n"
         << COLOR_GREEN "Example: ./amplifier -i 0.03 0.6 -o 1 1.9 -s 0.5 -j 8\n"
         << COLOR_RESET;
}

Config parse_args(int argc, char* argv[]) {
    Config cfg = {0};
    bool has_i = false, has_o = false;
    //
    // for (int i = 1; i < argc; ++i) {
    //     string arg = argv[i];
    //     if (arg == "-i" && i+2 < argc) {
    //         cfg.x1 = stod(argv[++i]);
    //         cfg.x2 = stod(argv[++i]);
    //         has_i = true;
    //     } else if (arg == "-o" && i+2 < argc) {
    //         cfg.Vmin = stod(argv[++i]);
    //         cfg.Vmax = stod(argv[++i]);
    //         has_o = true;
    //     } else if (arg == "-s" && i+1 < argc) {
    //         cfg.step = stod(argv[++i]);
    //     } else if (arg == "-j" && i+1 < argc) {
    //         cfg.threads = stoi(argv[++i]);
    //     } else if (arg == "-h") {
    //         print_help();
    //         exit(0);
    //     } else {
    //         cerr << "Invalid parameter: " << arg << endl;
    //         print_help();
    //         exit(1);
    //     }
    // }
    //
    // if (!has_i || !has_o || !cfg.validate()) {
    //     print_help();
    //     exit(1);
    // }
    cfg.x1 = 30;
    cfg.x2 = 600;
    cfg.Vmin = 1000;
    cfg.Vmax = 2000;
    cfg.step = 0.2;
    cfg.threads = 16;
    return cfg;
}

void update_progress() {
    const int bar_width = 50;
    while (processed_counter < total_iterations) {
        double percentage = processed_counter / total_iterations;
        int pos = bar_width * percentage;

        cout << COLOR_CYAN "\r[";
        for (int i = 0; i < bar_width; ++i) {
            cout << (i <= pos ? "#" : " ");
        }
        cout << "] " << fixed << setprecision(1) << percentage * 100.0 << "%";
        cout.flush();
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void worker_thread(const Config& cfg, double start, double end) {
    for (double O1 = start; O1 < end; O1 += cfg.step) {
        // 确保不超过MAX_GAIN
        O1 = min(O1, MAX_GAIN);

        for (double O1max = O1 + cfg.step; O1max <= MAX_GAIN + EPSILON; O1max += cfg.step) {
            for (double O2 = MIN_GAIN; O2 <= MAX_GAIN + EPSILON; O2 += cfg.step) {
                for (double O2max = O2 + cfg.step; O2max <= MAX_GAIN + EPSILON; O2max += cfg.step) {

                    vector<double> gains = {
                        O1*O2, O1*O2max,
                        O1max*O2, O1max*O2max
                    };
                    sort(gains.begin(), gains.end());

                    // 验证逻辑
                    vector<double> thresholds = {
                        cfg.Vmin / gains[0],
                        cfg.Vmin / gains[1],
                        cfg.Vmin / gains[2]
                    };

                    if (!(cfg.x1 <= thresholds[0] &&
                        thresholds[0] <= thresholds[1] &&
                        thresholds[1] <= thresholds[2] &&
                        thresholds[2] <= cfg.x2)) continue;

                    bool valid = true;
                    valid &= (gains[0] * thresholds[0] <= cfg.Vmax + EPSILON);
                    valid &= (gains[1] * thresholds[1] <= cfg.Vmax + EPSILON);
                    valid &= (gains[2] * thresholds[2] <= cfg.Vmax + EPSILON);
                    valid &= (gains[3] * cfg.x2 <= cfg.Vmax + EPSILON);

                    if (valid) {
                        Solution sol{
                            O1, O1max, O2, O2max,
                            gains, thresholds
                        };

                        lock_guard<mutex> lock(solutions_mutex);
                        global_solutions.push_back(sol);
                    }
                }
            }
        }
        ++processed_counter;
    }
}

vector<Solution> parallel_search(const Config& cfg) {
    vector<thread> threads;
    const double range = MAX_GAIN - MIN_GAIN;
    const double chunk_size = range / cfg.threads;

    // 计算总迭代次数用于进度条
    total_iterations = (MAX_GAIN - MIN_GAIN) / cfg.step + 1;

    // 启动进度显示线程
    thread progress_thread(update_progress);

    // 创建工作线程
    for (int i = 0; i < cfg.threads; ++i) {
        double start = MIN_GAIN + i * chunk_size;
        double end = start + chunk_size;
        threads.emplace_back(worker_thread, cref(cfg), start, end);
    }

    // 等待工作线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 等待进度条线程结束
    progress_thread.join();

    return global_solutions;
}

void print_solutions(const vector<Solution>& sols, const Config& cfg) {
    cout << COLOR_RESET "\n\nFound " << sols.size() << " valid solutions:\n";

    for (size_t i=0; i<sols.size(); ++i) {
        const auto& sol = sols[i];
        cout << COLOR_GREEN "\n# Solution " << i+1 << " #####################\n" COLOR_RESET;
        cout << "Stage 1: [" << sol.O1min << " ~ " << sol.O1max << "]\n";
        cout << "Stage 2: [" << sol.O2min << " ~ " << sol.O2max << "]\n";

        cout << COLOR_YELLOW "Gains: ";
        for (auto g : sol.gains) cout << g << " ";
        cout << COLOR_RESET "\nThresholds: ";
        for (auto t : sol.thresholds) cout << t << " ";

        // 显示详细输出范围
        cout << "\nOutput Ranges:\n";
        vector<pair<double, double>> ranges = {
            {cfg.x1, sol.thresholds[0]},
            {sol.thresholds[0], sol.thresholds[1]},
            {sol.thresholds[1], sol.thresholds[2]},
            {sol.thresholds[2], cfg.x2}
        };

        for (size_t j=0; j<4; ++j) {
            double min_in = ranges[j].first;
            double max_in = ranges[j].second;
            double gain = sol.gains[j];
            double min_out = min_in * gain;
            double max_out = max_in * gain;

            // 新验证条件：输出范围必须在[Vmin, Vmax]内
            bool range_valid = (min_out >= cfg.Vmin - EPSILON) &&
                              (max_out <= cfg.Vmax + EPSILON);

            printf("[%5.3f, %5.3f] => [%5.3f, %5.3f] %s\n",
                  min_in, max_in,
                  min_out, max_out,
                  range_valid ? COLOR_GREEN "✓" COLOR_RESET : COLOR_YELLOW "⚠" COLOR_RESET);
        }
    }
}

int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    cout << COLOR_YELLOW "\nSearch Parameters:" COLOR_RESET
         << "\nInput:  [" << cfg.x1 << ", " << cfg.x2 << "]"
         << "\nOutput: [" << cfg.Vmin << ", " << cfg.Vmax << "]"
         << "\nStep:   " << cfg.step
         << "\nThreads: " << cfg.threads << "\n\n";

    auto solutions = parallel_search(cfg);
    print_solutions(solutions, cfg);

    cout << COLOR_GREEN "\nSearch completed." COLOR_RESET << endl;
    return 0;
}
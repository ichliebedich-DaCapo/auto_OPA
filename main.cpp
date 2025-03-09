#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <string>
#include <sstream>

using namespace std;

// ANSI颜色代码
#define COLOR_RESET   "\033[0m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define PROGRESS_BAR  COLOR_CYAN "#" COLOR_RESET

// 配置参数
const double MIN_GAIN = 1.0;
const double MAX_GAIN = 100.0;
const double EPSILON = 1e-6; // 浮点比较容差

struct Config {
    double x1, x2;
    double Vmin, Vmax;
    double step = 1.0;

    bool validate() const {
        return (x1 < x2) && (Vmin < Vmax) && (step > EPSILON);
    }
};

struct Solution {
    double O1min, O1max;
    double O2min, O2max;
    vector<double> gains;
    vector<double> thresholds;
};

void print_help() {
    cout << COLOR_YELLOW
         << "Two-stage Amplifier Optimizer\n"
         << COLOR_RESET "Usage: ./amplifier -i <in_min> <in_max> -o <out_min> <out_max> [options]\n\n"
         << "Required Parameters:\n"
         << "  -i    Input voltage range (e.g. -i 0.03 0.6)\n"
         << "  -o    Output voltage range (e.g. -o 1 1.9)\n\n"
         << "Options:\n"
         << "  -s    Gain step size (default: 1.0)\n"
         << "  -h    Show this help\n\n"
         << COLOR_GREEN "Example: ./amplifier -i 0.03 0.6 -o 1 1.9 -s 0.5\n"
         << COLOR_RESET;
}

Config parse_args(int argc, char* argv[]) {
    Config cfg = {0};
    bool has_i = false, has_o = false;

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
    cfg.step = 1;
    return cfg;
}

void show_progress(double percentage) {
    const int bar_width = 50;
    int pos = bar_width * percentage;

    cout << COLOR_CYAN "\r[";
    for (int i = 0; i < bar_width; ++i) {
        cout << (i <= pos ? PROGRESS_BAR : " ");
    }
    cout << "] " << fixed << setprecision(1) << percentage * 100.0 << "%";
    cout.flush();
}

vector<Solution> find_solutions(const Config& cfg) {
    vector<Solution> solutions;
    const double total = (MAX_GAIN - MIN_GAIN)/cfg.step;
    double processed = 0;

    for (double O1 = MIN_GAIN; O1 <= MAX_GAIN + EPSILON; O1 += cfg.step) {
        show_progress(processed++ / total);

        for (double O1max = O1 + cfg.step; O1max <= MAX_GAIN + EPSILON; O1max += cfg.step) {
            for (double O2 = MIN_GAIN; O2 <= MAX_GAIN + EPSILON; O2 += cfg.step) {
                for (double O2max = O2 + cfg.step; O2max <= MAX_GAIN + EPSILON; O2max += cfg.step) {

                    vector<double> gains = {
                        O1*O2, O1*O2max,
                        O1max*O2, O1max*O2max
                    };
                    sort(gains.rbegin(), gains.rend());

                    // 快速过滤条件
                    if (gains.back()*cfg.x2 < cfg.Vmin) continue;
                    if (gains.front()*cfg.x1 > cfg.Vmax) continue;

                    // 计算分割点
                    vector<double> thresholds = {
                        min(cfg.Vmax/gains[0], cfg.x2),
                        min(cfg.Vmax/gains[1], cfg.x2),
                        min(cfg.Vmax/gains[2], cfg.x2)
                    };

                    // 验证区间
                    if (!is_sorted(thresholds.begin(), thresholds.end(),
                        [&](double a, double b){ return a <= b + EPSILON; })) continue;

                    // 验证输出范围
                    bool valid = true;
                    valid &= (gains[0]*cfg.x1 >= cfg.Vmin - EPSILON);
                    valid &= (gains[3]*cfg.x2 <= cfg.Vmax + EPSILON);

                    if (valid) {
                        solutions.push_back({
                            O1, O1max, O2, O2max,
                            gains, thresholds
                        });
                    }
                }
            }
        }
    }
    return solutions;
}

void print_solutions(const vector<Solution>& sols, const Config& cfg) {
    cout << COLOR_RESET "\n\nFound " << sols.size() << " valid solutions:\n";

    for (const auto& sol : sols) {
        cout << COLOR_GREEN "\n=============================\n" COLOR_RESET;
        cout << "Stage 1: [" << sol.O1min << " ~ " << sol.O1max << "]\n";
        cout << "Stage 2: [" << sol.O2min << " ~ " << sol.O2max << "]\n";

        cout << COLOR_YELLOW "Gains: ";
        for (auto g : sol.gains) cout << g << " ";
        cout << COLOR_RESET "\nThresholds: ";
        for (const auto t : sol.thresholds) cout << t << " ";

        // 显示详细输出范围
        cout << "\nOutput Ranges:\n";
        vector<pair<double, double>> ranges = {
            {cfg.x1, sol.thresholds[0]},
            {sol.thresholds[0], sol.thresholds[1]},
            {sol.thresholds[1], sol.thresholds[2]},
            {sol.thresholds[2], cfg.x2}
        };

        for (size_t i=0; i<4; ++i) {
            double min_out = ranges[i].first * sol.gains[i];
            double max_out = ranges[i].second * sol.gains[i];
            printf("[%5.3f, %5.3f] => [%5.3f, %5.3f]\n",
                  ranges[i].first, ranges[i].second, min_out, max_out);
        }
    }
}

int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    cout << COLOR_YELLOW "\nSearch Parameters:" COLOR_RESET
         << "\nInput:  [" << cfg.x1 << ", " << cfg.x2 << "]"
         << "\nOutput: [" << cfg.Vmin << ", " << cfg.Vmax << "]"
         << "\nStep:   " << cfg.step << "\n\n";

    auto solutions = find_solutions(cfg);
    print_solutions(solutions, cfg);

    cout << COLOR_GREEN "\nSearch completed." COLOR_RESET << endl;
    return 0;
}
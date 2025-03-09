#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <getopt.h>

using namespace std;

struct Result {
    double O1a, O1b, O2a, O2b;
    double s1, s2, s3;
};

vector<Result> results;
mutex results_mutex;

void worker(const vector<pair<double, double>>& O1_pairs,
            const vector<pair<double, double>>& O2_pairs,
            double x1, double x2, double vmin, double vmax,
            atomic<size_t>& processed_pairs) {
    for (const auto& o1_pair : O1_pairs) {
        double O1a = o1_pair.first;
        double O1b = o1_pair.second;
        for (const auto& o2_pair : O2_pairs) {
            double O2a = o2_pair.first;
            double O2b = o2_pair.second;

            double a = O1a * O2a;
            double b = O1a * O2b;
            double c = O1b * O2a;
            double d = O1b * O2b;
            vector<double> ks = {a, b, c, d};
            sort(ks.rbegin(), ks.rend());
            double k1 = ks[0], k2 = ks[1], k3 = ks[2], k4 = ks[3];

            if (k1 > vmax / x1) continue;
            if (k4 < vmin / x2) continue;

            double s1 = max(x1, vmin / k2);
            if (s1 > vmax / k1) continue;

            double s2 = max(s1, vmin / k3);
            if (s2 > vmax / k2) continue;

            double s3 = max(s2, vmin / k4);
            if (s3 > vmax / k3 || s3 > x2) continue;

            if (!(s1 < s2 && s2 < s3)) continue;

            lock_guard<mutex> lock(results_mutex);
            results.push_back({O1a, O1b, O2a, O2b, s1, s2, s3});
        }
        processed_pairs++;
    }
}

void print_progress(size_t processed, size_t total, size_t found) {
    const int bar_width = 50;
    float progress = static_cast<float>(processed) / total;
    int pos = bar_width * progress;
    string bar;
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) bar += "=";
        else if (i == pos) bar += ">";
        else bar += " ";
    }
    cout << "\033[34m\rProcessing: [" << bar << "] "
         << fixed << setprecision(1) << (progress * 100.0) << "%"
         << " Found: " << found << "\033[0m" << flush;
}

int main(int argc, char* argv[]) {
    double x1 = 30, x2 = 600, vmin = 1000, vmax = 1900, step = 0.5;
    int threads = 16;

    // 解析命令行参数
    // int opt;
    // while ((opt = getopt(argc, argv, "i:o:s:j:h")) != -1) {
    //     switch (opt) {
    //         case 'i':
    //             x1 = stod(optarg);
    //             x2 = stod(argv[optind++]);
    //             break;
    //         case 'o':
    //             vmin = stod(optarg);
    //             vmax = stod(argv[optind++]);
    //             break;
    //         case 's':
    //             step = stod(optarg);
    //             break;
    //         case 'j':
    //             threads = stoi(optarg);
    //             break;
    //         case 'h':
    //             cout << "Usage: " << argv[0] << " -i x1 x2 -o vmin vmax -s step -j threads\n";
    //             return 0;
    //         default:
    //             cerr << "Invalid arguments\n";
    //             return 1;
    //     }
    // }

    // 生成O1和O2的可能值
    vector<double> O1_values, O2_values;
    for (double o = step; o <= vmax / x1; o += step) {
        O1_values.push_back(o);
        O2_values.push_back(o);
    }

    // 生成O1和O2的两两组合
    vector<pair<double, double>> O1_pairs, O2_pairs;
    for (size_t i = 0; i < O1_values.size(); ++i) {
        for (size_t j = i + 1; j < O1_values.size(); ++j) {
            O1_pairs.emplace_back(O1_values[i], O1_values[j]);
        }
    }
    for (size_t i = 0; i < O2_values.size(); ++i) {
        for (size_t j = i + 1; j < O2_values.size(); ++j) {
            O2_pairs.emplace_back(O2_values[i], O2_values[j]);
        }
    }

    size_t total_pairs = O1_pairs.size();
    atomic<size_t> processed_pairs(0);

    // 分配任务给线程
    vector<thread> workers;
    size_t chunk_size = (O1_pairs.size() + threads - 1) / threads;

    for (int i = 0; i < threads; ++i) {
        size_t start = i * chunk_size;
        size_t end = min(start + chunk_size, O1_pairs.size());
        vector<pair<double, double>> chunk(O1_pairs.begin() + start, O1_pairs.begin() + end);
        workers.emplace_back(worker, chunk, O2_pairs, x1, x2, vmin, vmax, ref(processed_pairs));
    }

    // 显示进度条
    while (processed_pairs < total_pairs) {
        size_t current = processed_pairs.load();
        size_t found = results.size();
        print_progress(current, total_pairs, found);
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    for (auto& t : workers) {
        t.join();
    }
    print_progress(total_pairs, total_pairs, results.size());
    cout << endl;

    // 输出结果
    cout << "Found " << results.size() << " results:\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        cout << "Result " << i + 1 << ":\n";
        cout << "O1: [" << r.O1a << ", " << r.O1b << "]\n";
        cout << "O2: [" << r.O2a << ", " << r.O2b << "]\n";
        cout << "Segments: [" << x1 << ", " << r.s1 << "), ["
             << r.s1 << ", " << r.s2 << "), ["
             << r.s2 << ", " << r.s3 << "), ["
             << r.s3 << ", " << x2 << "]\n\n";
    }

    return 0;
}
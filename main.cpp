#include <iostream>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <string>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace std;

// ANSI颜色代码
#define BLUE    "\033[34m"
#define GREEN   "\033[32m"
#define RESET   "\033[0m"

struct Result {
    double O1min, O1max, O2min, O2max;
    vector<double> gains;
    vector<double> split_points;
};

vector<Result> global_results;
mutex results_mutex;
atomic<int> processed(0);
atomic<int> found_results(0);

vector<double> generate_O_values(double x, double Vmax, double step) {
    vector<double> values;
    double max_O = Vmax / x;
    for (double v = step; v <= max_O + 1e-9; v += step) {
        values.push_back(v);
    }
    return values;
}

void display_progress(int total, int found) {
    float progress = static_cast<float>(processed) / total;
    int bar_width = 50;
    cout << BLUE << "\r[";
    int pos = bar_width * progress;
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) cout << "=";
        else if (i == pos) cout << ">";
        else cout << " ";
    }
    cout << "] " << int(progress * 100.0) << "%" << RESET;
    cout << " " << GREEN << "Found: " << found << RESET << flush;
}

void display_thread_func(int total) {
    while (processed < total) {
        this_thread::sleep_for(chrono::milliseconds(100));
        int current_processed = processed.load();
        int current_found = found_results.load();
        display_progress(total, current_found);
    }
    display_progress(total, found_results.load());
    cout << endl;
}

void worker(const vector<pair<int, int>>& O1_combs, const vector<pair<int, int>>& O2_combs,
            const vector<double>& O1_values, const vector<double>& O2_values,
            double x1, double x2, double Vmin, double Vmax, int total_O1_combs) {
    while (true) {
        int idx = processed.fetch_add(1);
        if (idx >= total_O1_combs) break;

        auto& O1_pair = O1_combs[idx];
        double O1min = O1_values[O1_pair.first];
        double O1max = O1_values[O1_pair.second];

        for (auto& O2_pair : O2_combs) {
            double O2min = O2_values[O2_pair.first];
            double O2max = O2_values[O2_pair.second];

            double k1 = O1min * O2min;
            double k2 = O1min * O2max;
            double k3 = O1max * O2min;
            double k4 = O1max * O2max;

            if (k1 == k2 || k1 == k3 || k1 == k4 || k2 == k3 || k2 == k4 || k3 == k4) continue;

            vector<double> gains = {k1, k2, k3, k4};
            sort(gains.begin(), gains.end());

            do {
                if (gains[3] < Vmin/x2 - 1e-9 || gains[3] > Vmax/x2 + 1e-9) continue;
                if (gains[0] < Vmin/x1 - 1e-9) continue;

                double d0 = x1;
                double d1_low = max(d0, Vmin/gains[1]);
                double d1_high = Vmax/gains[0];
                if (d1_low > d1_high + 1e-9) continue;

                double d1 = d1_low;
                double d2_low = max(d1, Vmin/gains[2]);
                double d2_high = Vmax/gains[1];
                if (d2_low > d2_high + 1e-9) continue;

                double d2 = d2_low;
                double d3_low = max(d2, Vmin/gains[3]);
                double d3_high = min(Vmax/gains[2], x2);
                if (d3_low > d3_high + 1e-9) continue;

                double d3 = d3_low;
                if (d3 > x2 + 1e-9) continue;

                Result res;
                res.O1min = O1min;
                res.O1max = O1max;
                res.O2min = O2min;
                res.O2max = O2max;
                res.gains = gains;
                res.split_points = {d1, d2, d3};

                lock_guard<mutex> lock(results_mutex);
                global_results.push_back(res);
                found_results.fetch_add(1);
            } while (next_permutation(gains.begin(), gains.end()));
        }
    }
}

int main(int argc, char* argv[]) {
    double x1 = 30, x2 = 600, Vmin = 800, Vmax = 1900, step = 0.5;
    int threads = 16;

    // 解析命令行参数（示例参数，需根据实际输入处理）
    // 实际应用中应添加参数解析逻辑

    auto O1_values = generate_O_values(x1, Vmax, step);
    auto O2_values = generate_O_values(x1, Vmax, step);

    vector<pair<int, int>> O1_combs;
    for (int i = 0; i < O1_values.size(); ++i)
        for (int j = i+1; j < O1_values.size(); ++j)
            O1_combs.emplace_back(i, j);

    vector<pair<int, int>> O2_combs;
    for (int i = 0; i < O2_values.size(); ++i)
        for (int j = i+1; j < O2_values.size(); ++j)
            O2_combs.emplace_back(i, j);

    int total_O1_combs = O1_combs.size();

    thread display_thread(display_thread_func, total_O1_combs);
    vector<thread> workers;
    for (int i = 0; i < threads; ++i)
        workers.emplace_back(worker, ref(O1_combs), ref(O2_combs), ref(O1_values),
                             ref(O2_values), x1, x2, Vmin, Vmax, total_O1_combs);

    for (auto& t : workers) t.join();
    display_thread.join();

    ofstream out("results.txt");
    for (int i = 0; i < global_results.size(); ++i) {
        auto& r = global_results[i];
        out << "Result " << i+1 << ":\n";
        out << fixed << setprecision(6);
        out << "O1: [" << r.O1min << ", " << r.O1max << "]\n";
        out << "O2: [" << r.O2min << ", " << r.O2max << "]\n";
        out << "Gains: ";
        for (auto g : r.gains) out << g << " ";
        out << "\nSplit Points: ";
        for (auto s : r.split_points) out << s << " ";
        out << "\n\n";
    }

    return 0;
}
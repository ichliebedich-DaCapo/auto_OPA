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
#include <getopt.h>

using namespace std;

// ANSI颜色代码
#define BLUE    "\033[34m"
#define GREEN   "\033[32m"
#define RESET   "\033[0m"

struct Result
{
    double O1min{}, O1max{}, O2min{}, O2max{};
    vector<double> gains;
    vector<double> split_points;
};

vector<Result> global_results;
mutex results_mutex;
atomic<int> processed(0);
atomic<int> found_results(0);

vector<double> generate_O_values(double x, double Vmax, double step)
{
    vector<double> values;
    double max_O = Vmax / x;
    for (double v = step; v <= max_O + 1e-9; v += step)
    {
        values.push_back(v);
    }
    return values;
}

void display_progress(int total, int found)
{
    const float progress = static_cast<float>(processed) / total;
    const int bar_width = 50;
    cout << BLUE << "\r[";
    const int pos = bar_width * progress;
    for (int i = 0; i < bar_width; ++i)
    {
        if (i < pos) cout << "=";
        else if (i == pos) cout << ">";
        else cout << " ";
    }
    cout << "] " << static_cast<int>(progress * 100.0) << "%" << RESET;
    cout << " " << GREEN << "Found: " << found << RESET << flush;
}

void display_thread_func(int total)
{
    while (processed < total)
    {
        this_thread::sleep_for(chrono::milliseconds(100));
        int current_processed = processed.load();
        int current_found = found_results.load();
        display_progress(total, current_found);
    }
    display_progress(total, found_results.load());
    cout << endl;
}

void worker(const vector<pair<int, int> > &O1_combs, const vector<pair<int, int> > &O2_combs,
            const vector<double> &O1_values, const vector<double> &O2_values,
            double x1, double x2, double Vmin, double Vmax, int total_O1_combs)
{
    while (true)
    {
        int idx = processed.fetch_add(1);
        if (idx >= total_O1_combs) break;

        const auto &[fst, snd] = O1_combs[idx];
        const double O1min = O1_values[fst];
        const double O1max = O1_values[snd];

        for (auto &O2_pair: O2_combs)
        {
            const double O2min = O2_values[O2_pair.first];
            const double O2max = O2_values[O2_pair.second];

            const double k1 = O1min * O2min;
            const double k2 = O1min * O2max;
            const double k3 = O1max * O2min;
            const double k4 = O1max * O2max;

            if (k1 == k2 || k1 == k3 || k1 == k4 || k2 == k3 || k2 == k4 || k3 == k4) continue;

            vector<double> gains = {k1, k2, k3, k4};
            ranges::sort(gains);

            do
            {
                if (gains[3] < Vmin / x2 - 1e-9 || gains[3] > Vmax / x2 + 1e-9) continue;
                if (gains[0] < Vmin / x1 - 1e-9) continue;

                double d0 = x1;
                double d1_low = max(d0, Vmin / gains[1]);
                double d1_high = Vmax / gains[0];
                if (d1_low > d1_high + 1e-9) continue;

                double d1 = d1_low;
                double d2_low = max(d1, Vmin / gains[2]);
                double d2_high = Vmax / gains[1];
                if (d2_low > d2_high + 1e-9) continue;

                double d2 = d2_low;
                const double d3_low = max(d2, Vmin / gains[3]);
                const double d3_high = min(Vmax / gains[2], x2);
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
            } while (ranges::next_permutation(gains).found);
        }
    }
}

int main(int argc, char *argv[])
{
    double x1 = 30, x2 = 600, Vmin = 875, Vmax = 1950, step = 0.5;
    int threads = 16;

    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "i:o:s:j:h")) != -1)
    {
        switch (opt)
        {
            case 'i':
                x1 = stod(optarg);
                x2 = stod(argv[optind++]);
                break;
            case 'o':
                Vmin = stod(optarg);
                Vmax = stod(argv[optind++]);
                break;
            case 's':
                step = stod(optarg);
                break;
            case 'j':
                threads = stoi(optarg);
                break;
            case 'h':
                // 中文会乱码
                // cout << "[-i]:输入区间范围，比如-i 0.03 0.6\n"
                //         << "[-o]:输出区间范围，比如-o 0.9 2\n"
                //         << "[-s]:步长，比如-s 0.5\n"
                //         << "[-j]:线程数，比如-j 16\n"
                //         << "[-h]:帮助信息\n";;
                cout << "Two-Stage Programmable Amplifier Configuration Finder\n\n"
                        << "Usage:\n"
                        << "  ./auto_OPA -i <x_low> <x_high> -o <Vmin> <Vmax> [options]\n\n"
                        << "Required Parameters:\n"
                        << "  -i  Input voltage range (left-closed right-open interval)\n"
                        << "      Example: -i 0.03 0.6\n"
                        << "  -o  Desired output voltage range\n"
                        << "      Example: -o 1.0 3.3\n\n"
                        << "Options:\n"
                        << "  -s  Step size for gain search (default: 0.1)\n"
                        << "  -j  Number of parallel threads (default: CPU core count)\n"
                        << "  -h  Display this help message\n\n"
                        << "Validation Criteria:\n"
                        << "  1. Input coverage: [x_low, x_high] must be fully covered\n"
                        << "  2. Output constraint: ∀x∈[x_low,x_high], Vmin ≤ x·gain ≤ Vmax\n"
                        << "  3. Gain continuity: Adjacent regions must have overlapping gains\n";
                exit(1);
            default:
                cerr << "Usage: " << argv[0]
                        << " -i x_low x_high -o Vmin Vmax -s step -j threads\n";
                exit(1);
        }
    }

    // 打印参数
    cout << "Vin  [" << x1 << "," << x2 << "]\nVout [" << Vmin << "," << Vmax << "]\nstep=" << step << "\nthreads=" <<
            threads << endl;

    auto O1_values = generate_O_values(x1, Vmax, step);
    auto O2_values = generate_O_values(x1, Vmax, step);

    vector<pair<int, int> > O1_combs;
    for (int i = 0; i < O1_values.size(); ++i)
        for (int j = i + 1; j < O1_values.size(); ++j)
            O1_combs.emplace_back(i, j);

    vector<pair<int, int> > O2_combs;
    for (int i = 0; i < O2_values.size(); ++i)
        for (int j = i + 1; j < O2_values.size(); ++j)
            O2_combs.emplace_back(i, j);

    int total_O1_combs = O1_combs.size();

    thread display_thread(display_thread_func, total_O1_combs);
    vector<thread> workers;
    for (int i = 0; i < threads; ++i)
        workers.emplace_back(worker, ref(O1_combs), ref(O2_combs), ref(O1_values),
                             ref(O2_values), x1, x2, Vmin, Vmax, total_O1_combs);

    for (auto &t: workers) t.join();
    display_thread.join();

    ofstream out("results.txt");
    for (int i = 0; i < global_results.size(); ++i)
    {
        auto &[O1min, O1max, O2min, O2max, gains, split_points] = global_results[i];
        out << "Result " << i + 1 << ":\n";
        out << fixed << setprecision(6);
        out << "O1: [" << O1min << ", " << O1max << "]\n";
        out << "O2: [" << O2min << ", " << O2max << "]\n";
        out << "Gains: ";
        for (auto g: gains) out << g << " ";
        out << "\nSplit Points: ";
        for (auto s: split_points) out << s << " ";
        // 增益区间范围
        out << "\nGains Zone: ";
        double gains_zone_low = x1;
        for (int n = 0; n < 4; ++n)
        {
            out << "[" << gains[n] * gains_zone_low << "," << gains[n] * (n == 3 ? x2 : split_points[n]) << "] ";
            gains_zone_low = split_points[n];
        }
        out << "\n\n";
    }

    return 0;
}

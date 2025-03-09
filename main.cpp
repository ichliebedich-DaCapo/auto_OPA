#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <getopt.h>

using namespace std;
// ANSI颜色代码
#define COLOR_RESET   "\033[0m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"

struct Params
{
    double x1{}, x2{};
    double v1{}, v2{};
    double step{};
    int threads{};
    bool valid = false;
};

struct Result
{
    double O1min, O1max;
    double O2min, O2max;
    vector<pair<double, double> > intervals;
};

vector<Result> results;
mutex results_mutex;
atomic<long long> progress(0);
long long total_combinations = 0;

Params parse_args(int argc, char *argv[])
{
    Params p;
    int opt;
    // while ((opt = getopt(argc, argv, "i:o:s:j:h")) != -1) {
    //     switch (opt) {
    //         case 'i':
    //             p.x1 = stod(optarg);
    //             p.x2 = stod(argv[optind++]);
    //             break;
    //         case 'o':
    //             p.v1 = stod(optarg);
    //             p.v2 = stod(argv[optind++]);
    //             break;
    //         case 's':
    //             p.step = stod(optarg);
    //             break;
    //         case 'j':
    //             p.threads = stoi(optarg);
    //             break;
    //         case 'h':
    //         default:
    //             cerr << "Usage: " << argv[0]
    //                  << " -i x1 x2 -o v1 v2 -s step -j threads\n";
    //             exit(1);
    //     }
    // }
    // if (p.x2 <= p.x1 || p.v2 <= p.v1 || p.step <= 0 || p.threads <= 0) {
    //     cerr << "Invalid parameters\n";
    //     exit(1);
    // }
    p.x1 = 30;
    p.x2 = 600;
    p.v1 = 500;
    p.v2 = 1900;
    p.step = 0.2;
    p.threads = 16;
    p.valid = true;
    return p;
}

void calculate_range(const Params &p,
                     double o1min_start, double o1min_end,
                     double o2min_start, double o2min_end)
{
    for (double O1min = o1min_start; O1min <= o1min_end; O1min += p.step)
    {
        for (double O1max = O1min + p.step; O1max <= p.v2 / p.x1; O1max += p.step)
        {
            for (double O2min = o2min_start; O2min <= o2min_end; O2min += p.step)
            {
                for (double O2max = O2min + p.step; O2max <= p.v2 / p.x1; O2max += p.step)
                {
                    double gains[4] = {
                        O1min * O2min,
                        O1min * O2max,
                        O1max * O2min,
                        O1max * O2max
                    };
                    sort(gains, gains + 4, greater<double>());

                    double L[4], R[4];
                    for (int i = 0; i < 4; ++i)
                    {
                        L[i] = p.v1 / gains[i];
                        R[i] = p.v2 / gains[i];
                    }

                    vector<pair<double, double> > intervals;
                    double prev_end = p.x1;
                    bool valid = true;
                    for (int i = 0; i < 4; ++i)
                    {
                        double start = max(prev_end, L[i]);
                        double end = (i == 3) ? p.x2 : R[i];
                        if (start > end)
                        {
                            valid = false;
                            break;
                        }
                        if (end > p.x2) end = p.x2;
                        intervals.emplace_back(start, end);
                        prev_end = end;
                    }

                    if (valid && prev_end >= p.x2 - 1e-9)
                    {
                        Result res{O1min, O1max, O2min, O2max, intervals};
                        lock_guard<mutex> lock(results_mutex);
                        results.push_back(res);
                    }
                    ++progress;
                }
            }
        }
    }
}

void show_progress(const Params &p)
{
    while (progress < total_combinations)
    {
        double percent = 100.0 * progress / total_combinations;
        cout << "\033[34mProgress: [";
        int pos = 50 * progress / total_combinations;
        for (int i = 0; i < 50; ++i)
            cout << (i < pos ? '=' : (i == pos ? '>' : ' '));
        cout << "] " << fixed << setprecision(1) << percent << "%\r";
        cout.flush();
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

int main(int argc, char *argv[])
{
    Params p = parse_args(argc, argv);
    if (!p.valid) return 1;

    vector<thread> threads;
    int num_per_thread = ceil((p.v2 / p.x1) / p.step / p.threads);

    for (int i = 0; i < p.threads; ++i)
    {
        threads.emplace_back([&p, i, num_per_thread]
        {
            const double o1min_start = i * num_per_thread * p.step;
            const double o1min_end = min(o1min_start + num_per_thread * p.step, p.v2 / p.x1);
            calculate_range(p, o1min_start, o1min_end, 0, p.v2 / p.x1);
        });
    }

    thread progress_thread(show_progress, cref(p));

    for (auto &t: threads) t.join();
    progress_thread.join();

    cout << "\nFound " << results.size() << " results:\n";
    for (size_t i = 0; i < results.size(); ++i)
    {
        cout << "Result " << i + 1 << ":\n";
        printf("O1: [%.4f, %.4f]  O2: [%.4f, %.4f]\n",
               results[i].O1min, results[i].O1max,
               results[i].O2min, results[i].O2max);
        cout << "Intervals:\n";
        for (const auto &[fst, snd]: results[i].intervals)
        {
            printf("[%.6f, %.6f]\n", fst, snd);
        }
        cout << endl;
    }
}

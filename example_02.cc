#include <random>
#include <vector>

#include <memstats.hh>

volatile void * do_not_optimize;

int main()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    for (int rep = 1; rep != 4; ++rep) {
        // only instrument a part of the code
        MemStats::enable_instrumentation();
        std::normal_distribution<> distrib(rep*100, 50);
        for (int i = 0; i != 10000; ++i) {
            std::vector<double> v(std::abs(distrib(gen)));
            do_not_optimize = v.data();
        }
        MemStats::disable_instrumentation();
        MemStats::report("report " + std::to_string(rep) );
    }
    {
        // this part is not instrumented
        std::normal_distribution<> distrib(200, 65);
        for (int i = 0; i != 10000; ++i) {
            std::vector<double> v(std::abs(distrib(gen)));
            do_not_optimize = v.data();
        }
    }
}

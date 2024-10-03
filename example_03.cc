#include <random>
#include <vector>
#include <thread>

#include <memstats.hh>

volatile void * do_not_optimize;

int main()
{
    std::vector<std::thread> threads;
    for (int rep = 1; rep != 4; ++rep) {
        threads.emplace_back([=]{
            // instrument a part of the code
            memstats_enable_thread_instrumentation();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::normal_distribution<> distrib(rep*100, 50);
            for (int i = 0; i != 10000; ++i) {
                std::vector<double> v(std::abs(distrib(gen)));
                do_not_optimize = v.data();
            }
            memstats_disable_thread_instrumentation();
        });
    }
    for(auto& thread : threads)
        thread.join();
}

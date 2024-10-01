#include <random>
#include <vector>

volatile void * do_not_optimize;

int main()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    {
        std::normal_distribution<> distrib(400, 50);
        for (int i = 0; i != 10000; ++i) {
            std::vector<double> v(std::abs(distrib(gen)));
            do_not_optimize = v.data();
        }
    }
    {
        std::normal_distribution<> distrib(200, 65);
        for (int i = 0; i != 10000; ++i) {
            std::vector<double> v(std::abs(distrib(gen)));
            do_not_optimize = v.data();
        }
    }
}

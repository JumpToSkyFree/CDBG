#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct particle {
    double mass;
    double velocity;

    double energy() const {
        return 0.5 * mass * velocity * velocity;
    }
};

int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int classify(int value) {
    if (value < 0) {
        return -1;
    } else if (value == 0) {
        return 0;
    } else if (value < 10) {
        return 1;
    }
    return 2;
}

void fill(std::vector<int> &values, int count) {
    for (int i = 0; i < count; ++i) {
        values.push_back(i * i);
    }
}

int sum_vector(const std::vector<int> &values) {
    int total = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        total += values[i];
    }
    return total;
}

int switch_on(int mode) {
    switch (mode) {
    case 0:
        return 10;
    case 1:
        return 20;
    case 2:
        return 30;
    default:
        return -1;
    }
}

template <typename T>
T clamp_value(T value, T low, T high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

std::string describe(int branch) {
    std::string out;
    if (branch < 0) {
        out = "negative";
    } else if (branch == 0) {
        out = "zero";
    } else {
        out = "positive";
    }
    return out;
}

int count_down(int from) {
    int steps = 0;
    while (from > 0) {
        from -= 2;
        steps += 1;
    }
    return steps;
}

} // namespace

int main() {
    std::vector<int> values;
    fill(values, 8);

    int total = sum_vector(values);

    particle p{2.0, 3.0};
    double energy = p.energy();

    int branch = classify(total);
    int mode = switch_on(branch);
    int f = fib(6);
    int steps = count_down(10);

    int acc = 0;
    for (int i = 0; i < 5; ++i) {
        acc += i * mode;
        if (acc > 100) {
            acc -= 50;
        }
    }

    auto scale = [](int v) { return v * 2; };
    int scaled = clamp_value(scale(acc), 0, 120);

    std::string label = describe(branch);

    std::printf("total=%d energy=%.1f branch=%d mode=%d fib=%d steps=%d "
                "acc=%d scaled=%d label=%s\n",
                total, energy, branch, mode, f, steps, acc, scaled,
                label.c_str());
    return scaled == 0 ? 1 : EXIT_SUCCESS;
}

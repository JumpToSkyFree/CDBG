#include <stdlib.h>

const int test_const() { return 1; }
const int *test_ptr() { return 0x00; }
const int &test_ref(const char *str) {
    static int l{0};
    return l;
}

int main(int argc, char *argv[]) {
    int var = 0;
    int x = 120;
    const int z = test_ref("HELLO WORLD");
    for (int i = 0; i < 100; i++) {
        var += i;
    }
    return EXIT_SUCCESS;
}

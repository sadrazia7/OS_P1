#include "kernel/types.h"
#include "user/user.h"

static void summarize(const char *label, int before, int after) {
    int diff = after - before;
    printf("%s: before=%d after=%d diff=%d\n", label, before, after, diff);

    if (diff < 1) {
        printf("FAIL: syscall count did not increase as expected\n");
    }
}

int main(int argc, char *argv[]) {
    int start = sysclcnt();
    if (start < 0) {
        printf("ERROR: sysclcnt() failed: %d\n", start);
        exit(-1);
    }

    printf("Initial count: %d\n", start);

    int a = sysclcnt();
    printf("hello from test\n");
    int b = sysclcnt();
    summarize("single-block", a, b);
    int c = sysclcnt();

    for (int i = 0; i < 5; i++) {
        getpid();
        sleep(1);
        printf("i=%d\n", i);
    }

    int d = sysclcnt();
    summarize("multi-block", c, d);

    for (int i = 0; i < 3; i++) {
        int before = sysclcnt();
        printf("round %d\n", i);
        int after = sysclcnt();

        if (after < before) {
            printf("FAIL: non-monotonic counter! before=%d after=%d\n", before, after);
            exit(-1);
        }
    }

    printf("All tests completed.\n");
    exit(0);
}


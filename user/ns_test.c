#include "kernel/types.h"
#include "user/user.h"

#define CLONE_NEWPID 0x20000000

int main() {
    printf("--- Starting PID Namespace Test ---\n");

    if (unshare(CLONE_NEWPID) < 0) {
        printf("Unshare failed\n");
        exit(1);
    }

    int pid = fork();

    if (pid < 0) {
        printf("Fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        // child
        printf("Child PID inside namespace: %d\n", getpid());
        exit(0);
    } else {
        wait(0);
        printf("Parent sees child PID: %d\n", pid);
    }

    exit(0);
}

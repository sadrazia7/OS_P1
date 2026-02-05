#include "kernel/types.h"
#include "user/user.h"

#define CLONE_NEWPID 0x01 // [cite: 247]

int main() {
    printf("--- Starting Namespace Test ---\n");
    if(unshare(CLONE_NEWPID) < 0){ // جدا کردن PID Namespace [cite: 247]
        printf("Unshare Failed!\n");
    } else {
        printf("Unshare Success! Process is now isolated.\n");
    }
    exit(0);
}
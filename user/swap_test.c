#include "kernel/types.h"
#include "user/user.h"

#define PAGES 10

int main() {
    printf("--- Starting Swap Test ---\n");

    int pid = fork();
    if(pid == 0){
        char *pages[PAGES];

        for(int i = 0; i < PAGES; i++){
            pages[i] = sbrk(4096);
            if(pages[i] == (char*)-1){
                printf("sbrk failed\n");
                exit(1);
            }
            pages[i][0] = i; // unique pattern
        }

        sleep(200); // force swap-out

        for(int i = 0; i < PAGES; i++){
            if(pages[i][0] != i){
                printf("Swap data corruption at page %d\n", i);
                exit(1);
            }
        }

        printf("Child swap verification passed\n");
        exit(0);
    }

    wait(0);
    printf("Swap Test Finished Successfully!\n");
    exit(0);
}

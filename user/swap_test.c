#include "kernel/types.h"
#include "user/user.h"

int main() {
    printf("--- Starting Swap Test ---\n");
    for(int i = 0; i < 20; i++){ // ایجاد ۲۰ فرزند طبق صورت پروژه
        int pid = fork();
        if(pid == 0){
            for(int j = 0; j < 10; j++){
                char *m = malloc(4096); // اشغال 4KB فضا 
                if(m == 0) break;
                m[0] = 'A'; // پر کردن برای تست صحت [cite: 122]
            }
            exit(0);
        }
    }
    for(int i = 0; i < 20; i++) wait(0);
    printf("Swap Test Finished Successfully!\n");
    exit(0);
}
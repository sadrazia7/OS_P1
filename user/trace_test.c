#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/user.h"

int
main(void)
{
  int pid;

  printf("=== TRACE TEST START ===\n");

  // Test 1: trace write only
  // (1 << SYS_write) یعنی فقط سیستم‌کال نوشتن ردیابی شود [cite: 290, 291]
  trace(1 << SYS_write);
  write(1, "A\n", 2);

  // Test 2: mask filtering
  // اینجا تلاش برای باز کردن فایل است، اما چون فقط write ردیابی می‌شود، نباید چیزی چاپ شود [cite: 293, 294]
  open("README", 0);

  // Test 3: multiple syscalls (write + exit)
  // ردیابی همزمان نوشتن و خروج [cite: 297, 299]
  trace((1 << SYS_write) | (1 << SYS_exit));
  write(1, "B\n", 2);

  // Test 4: fork inheritance
  // بررسی اینکه آیا فرزند هم تنظیمات ردیابی را از پدر به ارث می‌برد یا نه [cite: 304, 314]
  trace(1 << SYS_write);
  pid = fork();
  if (pid == 0) {
    write(1, "child\n", 6);
    exit(0);
  } else {
    write(1, "parent\n", 7);
    wait(0);
  }

  // Test 5: exec tracing
  // بررسی ردیابی در هنگام اجرای یک برنامه جدید [cite: 326, 328]
  trace(1 << SYS_exec);
  pid = fork();
  if (pid == 0) {
    char *argv[] = {"echo", "exec-ok", 0};
    exec("echo", argv);
    exit(1);
  } else {
    wait(0);
  }

  // Test 6: disable tracing
  // غیرفعال کردن ردیابی با فرستادن عدد صفر [cite: 349, 350]
  trace(0);
  write(1, "NO TRACE\n", 9);

  printf("=== TRACE TEST END ===\n");
  exit(0);
}

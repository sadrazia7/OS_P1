#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

#define CLONE_NEWPID 0x20000000
#define CLONE_NEWUTS 0x04000000
#define CLONE_NEWNS 0x00020000
#define CLONE_NEWIPC 0x08000000

void swap_out_daemon(void)
{
    // placeholder: not implemented yet
    for(;;){
        // اگر نذاری حلقه، ممکنه main برگرده
        asm volatile("wfi");
    }
}

void create_kernel_process(const char *name, void (*fn)(void)) {
    // فقط برای رفع ارور لینکینگ؛ کاری انجام نمیده
}

extern uint64 syscalls_counter;

// =====================
// Trace system call
// =====================
uint64
sys_trace(void)
{
    int mask;
    argint(0, &mask);               // فراخوانی argint بدون چک برگشتی
    myproc()->trace_mask = mask;     // ذخیره ماسک در پردازه فعلی
    return 0;
}

// =====================
// Unshare namespaces
// =====================
uint64
sys_unshare(void)
{
    int flags;
    struct proc *p = myproc();

    argint(0, &flags);

    // جدا کردن فضای نام PID
    if(flags & CLONE_NEWPID){
        if(p->pid_ns)
            p->pid_ns->refcount--;
        struct pid_namespace *new_ns = (struct pid_namespace*)kalloc();
        new_ns->refcount = 1;
        p->pid_ns = new_ns;
    }

    // جدا کردن فضای نام UTS
    if(flags & CLONE_NEWUTS){
        if(p->uts_ns)
            ; // در نسخه فعلی، refcount نداریم
        struct uts_namespace *new_uts = (struct uts_namespace*)kalloc();
        safestrcpy(new_uts->nodename, "xv6-new", sizeof(new_uts->nodename));
        p->uts_ns = new_uts;
    }

    if(flags & CLONE_NEWNS){
	if(p->mnt_ns)
	   p->mnt_ns->refcount--;

	struct mount_namespace *new_mnt = kalloc();
	memset(new_mnt, 0, sizeof(*new_mnt));
	new_mnt->refcount = 1;
	new_mnt->root = namei("/"); // root جدید

	p->mnt_ns = new_mnt;
	}

   if(flags & CLONE_NEWIPC){
	if(p->ipc_ns)
	   p->ipc_ns->refcount--;

	struct ipc_namespace *new_ipc = kalloc();
	new_ipc->refcount = 1;
	p->ipc_ns = new_ipc;
	}


    return 0;
}

// =====================
// System call counters
// =====================
uint64
sys_sysclcnt(void)
{
    return syscalls_counter;
}

// =====================
// Change process nice
// =====================
uint64
sys_chpnice(void)
{
    int pid, nice;
    argint(0, &pid);
    argint(1, &nice);
    return kchpnice(pid, nice);
}

// =====================
// Exit
// =====================
uint64
sys_exit(void)
{
    int n;
    argint(0, &n);
    kexit(n);
    return 0;  // not reached
}

// =====================
// Get pid
// =====================
uint64
sys_getpid(void)
{
    return myproc()->pid;
}

// =====================
// Fork
// =====================
uint64
sys_fork(void)
{
    return kfork();
}

// =====================
// Wait
// =====================
uint64
sys_wait(void)
{
    uint64 p;
    argaddr(0, &p);
    return kwait(p);
}

// =====================
// Sbrk
// =====================
uint64
sys_sbrk(void)
{
    uint64 addr;
    int t, n;

    argint(0, &n);
    argint(1, &t);
    addr = myproc()->sz;

    if(t == SBRK_EAGER || n < 0){
        if(growproc(n) < 0)
            return -1;
    } else {
        if(addr + n < addr)
            return -1;
        if(addr + n > TRAPFRAME)
            return -1;
        myproc()->sz += n;
    }

    return addr;
}

// =====================
// Pause
// =====================
uint64
sys_pause(void)
{
    int n;
    uint ticks0;

    argint(0, &n);
    if(n < 0)
        n = 0;

    acquire(&tickslock);
    ticks0 = ticks;
    while(ticks - ticks0 < n){
        if(killed(myproc())){
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

// =====================
// Kill
// =====================
uint64
sys_kill(void)
{
    int pid;
    argint(0, &pid);
    return kkill(pid);
}

// =====================
// Uptime
// =====================
uint64
sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}

// =====================
// Process tree
// =====================
uint64
sys_ptree(void)
{
    int pid;
    uint64 uaddr;
    extern struct proc_tree ptree_buf;

    argint(0, &pid);
    argaddr(1, &uaddr);

    if(build_ptree(pid) < 0)
        return -1;

    struct proc *p = myproc();
    if(copyout(p->pagetable, uaddr, (char *)&ptree_buf, sizeof(ptree_buf)) < 0)
        return -1;

    return 0;
}

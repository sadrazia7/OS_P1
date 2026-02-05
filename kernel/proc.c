#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct pid_namespace root_pid_ns = { .refcount = 1 };
struct uts_namespace root_uts_ns = { .nodename = "xv6" };
struct mount_namespace root_mnt_ns;
struct ipc_namespace root_ipc_ns;


static void
ns_incref(struct proc *p)
{
  if(p->pid_ns)
    p->pid_ns->refcount++;
}

static void
ns_decref(struct proc *p)
{
  if(p->pid_ns){
    p->pid_ns->refcount--;
    // فعلاً آزادسازی واقعی نداریم
  }
}

struct proc proc[NPROC];
struct proc *initproc;
int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
extern char trampoline[]; // trampoline.S

struct spinlock wait_lock;

#define NICE_0_WEIGHT 1024
#define CFS_TARGET_LATENCY_TICKS 5
#define CFS_MIN_GRANULARITY_TICKS 1

static int
nice_to_weight(int nice)
{
  int weight = NICE_0_WEIGHT;
  int i;

  if(nice > 19)
    nice = 19;
  if(nice < -20)
    nice = -20;

  if(nice > 0){
    for(i = 0; i < nice; i++)
      weight = (weight * 100) / 125;
  } else if(nice < 0){
    for(i = 0; i < -nice; i++)
      weight = (weight * 125) / 100;
  }

  if(weight < 1)
    weight = 1;
  return weight;
}

int
kchpnice(int pid, int nice)
{
  struct proc *p;

  if(pid <= 0)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED){
      p->nice = nice;
      p->weight = nice_to_weight(nice);
      p->slice_ticks = 0;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }

  return -1;
}

static uint64
cfs_now_ticks(void)
{
  uint64 now;

  acquire(&tickslock);
  now = ticks;
  release(&tickslock);
  return now;
}

static void
cfs_account_runtime(struct proc *p)
{
  uint64 now, delta, scaled;

  if(p == 0)
    return;

  now = cfs_now_ticks();
  if(p->last_run_tick == 0){
    p->last_run_tick = now;
    return;
  }

  delta = now - p->last_run_tick;
  if(delta == 0)
    return;

  p->last_run_tick = now;
  scaled = (delta * NICE_0_WEIGHT) / p->weight;
  if(scaled == 0)
    scaled = 1;
  p->vruntime += scaled;
}

static uint64
cfs_timeslice_ticks(struct proc *p)
{
  struct proc *q;
  uint64 total_weight = p->weight;
  uint64 ideal;

  for(q = proc; q < &proc[NPROC]; q++){
    if(q == p)
      continue;
    acquire(&q->lock);
    if(q->state == RUNNABLE)
      total_weight += q->weight;
    release(&q->lock);
  }

  if(total_weight == 0)
    total_weight = 1;

  ideal = (CFS_TARGET_LATENCY_TICKS * p->weight) / total_weight;
  if(ideal < CFS_MIN_GRANULARITY_TICKS)
    ideal = CFS_MIN_GRANULARITY_TICKS;
  return ideal;
}

static void
cfs_on_pick(struct proc *p)
{
  uint64 now = cfs_now_ticks();

  p->slice_ticks = cfs_timeslice_ticks(p);
  if(p->slice_ticks == 0)
    p->slice_ticks = CFS_MIN_GRANULARITY_TICKS;
  p->slice_start_tick = now;
  p->last_run_tick = now;
}

int
cfs_should_yield(struct proc *p)
{
  uint64 now;

  if(p == 0)
    return 0;
  if(p->slice_ticks == 0)
    return 1;

  now = cfs_now_ticks();
  return (now - p->slice_start_tick) >= p->slice_ticks;
}

// Allocate a page for each process's kernel stack.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->nice = 0;
  p->weight = nice_to_weight(p->nice);
  p->vruntime = 0;
  p->last_run_tick = 0;
  p->slice_start_tick = 0;
  p->slice_ticks = 0;
  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  p->trace_mask = 0;

  p->pid_ns = 0;
  p->uts_ns = 0;
  p->mnt_ns = 0;
  p->ipc_ns = 0;

  return p;
}

// free a proc structure and the data hanging from it
static void
freeproc(struct proc *p)
{
  ns_decref(p);

  p->pid_ns = 0;
  p->uts_ns = 0;
  p->mnt_ns = 0;
  p->ipc_ns = 0;

  p->trace_mask = 0;

  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->nice = 0;
  p->weight = 0;
  p->vruntime = 0;
  p->last_run_tick = 0;
  p->slice_start_tick = 0;
  p->slice_ticks = 0;
}

// Create a user page table for a given process
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->pid_ns = &root_pid_ns;
  p->uts_ns = &root_uts_ns;
  p->mnt_ns = &root_mnt_ns;
  p->ipc_ns = &root_ipc_ns;

  p->trace_mask = 0;
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME)
      return -1;
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
      return -1;
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  if((np = allocproc()) == 0)
    return -1;

  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  *(np->trapframe) = *(p->trapframe);
  np->trapframe->a0 = 0;

  np->trace_mask = p->trace_mask;
  np->pid_ns = p->pid_ns;
  np->uts_ns = p->uts_ns;
  np->mnt_ns = p->mnt_ns;
  np->ipc_ns = p->ipc_ns;
  ns_incref(np);

  np->nice = p->nice;
  np->weight = p->weight;
  np->vruntime = p->vruntime;
  np->last_run_tick = 0;
  np->slice_start_tick = 0;
  np->slice_ticks = 0;

  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));
  pid = np->pid;

  release(&np->lock);
  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = p->parent;
      wakeup(initproc);
    }
  }
}

// Exit the current process
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);
  reparent(p);
  wakeup(p->parent);

  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;
  release(&wait_lock);

  sched();
  panic("zombie exit");
}

// Wait for a child process
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        acquire(&pp->lock);
        havekids = 1;
        if(pp->state == ZOMBIE){
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }

    sleep(p, &wait_lock);
  }
}

// Per-CPU process scheduler
void
scheduler(void)
{
  struct proc *p;
  struct proc *best;
  uint64 best_vruntime;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();
    intr_off();

    best = 0;
    best_vruntime = 0;
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE){
        if(best == 0 || p->vruntime < best_vruntime){
          best = p;
          best_vruntime = p->vruntime;
        }
      }
      release(&p->lock);
    }

    if(best == 0){
      asm volatile("wfi");
      continue;
    }

    acquire(&best->lock);
    if(best->state != RUNNABLE){
      release(&best->lock);
      continue;
    }

    p = best;
    p->state = RUNNING;
    c->proc = p;
    cfs_on_pick(p);
    swtch(&c->context, &p->context);

    c->proc = 0;
    release(&p->lock);
  }
}

// Switch to scheduler
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  cfs_account_runtime(p);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's first scheduling
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  release(&p->lock);

  if(first){
    fsinit(ROOTDEV);
    first = 0;
    __sync_synchronize();
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if(p->trapframe->a0 == -1)
      panic("exec");
  }

  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  release(lk);

  cfs_account_runtime(p);

  p->chan = chan;
  p->state = SLEEPING;

  sched();

  p->chan = 0;
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan)
        p->state = RUNNABLE;
      release(&p->lock);
    }
  }
}

// Kill process with pid
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either user or kernel
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst)
    return copyout(p->pagetable, dst, src, len);
  else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either user or kernel
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src)
    return copyin(p->pagetable, dst, src, len);
  else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print process listing
void
procdump(void)
{
  static char *states[] = {
    [UNUSED]    "unused",
    [USED]      "used",
    [SLEEPING]  "sleep ",
    [RUNNABLE]  "runble",
    [RUNNING]   "run   ",
    [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s\n", p->pid, state, p->name);
  }
}

// ===== ptree implementation =====
#include "ptree_struct.h"

// یک بافر سراسری در کرنل برای درخت فرایندها
struct proc_tree ptree_buf;

// prototype
int build_ptree(int pid);
void build_children(struct proc *parent, int parent_index);

// فقط این دو تابع در سایر فایل‌ها دیده می‌شوند:
int  build_ptree(int pid);
void build_children(struct proc *parent, int parent_index);

// ساخت ریشه و پر کردن ptree_buf
int
build_ptree(int pid)
{
  struct proc *p;
  struct proc *root = 0;

  // پیدا کردن پروسه‌ی ریشه
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->pid == pid && p->state != UNUSED) {
      root = p;
      release(&p->lock);
      break;
    }
    release(&p->lock);
  }

  if(root == 0)
    return -1;

  // آماده‌سازی درخت در بافر سراسری
  ptree_buf.count = 0;

  int root_index = ptree_buf.count++;
  if(root_index >= NPROC)
    return -1;

  struct proc_info *rinfo = &ptree_buf.procs[root_index];
  safestrcpy(rinfo->name, root->name, sizeof(rinfo->name));
  rinfo->pid = root->pid;
  rinfo->child_count = 0;

  ptree_buf.root_index = root_index;

  // ساختن بچه‌ها
  build_children(root, root_index);

  return 0;
}

// بازگشتی: فرزندان یک parent را به درخت اضافه می‌کند
void
build_children(struct proc *parent, int parent_index)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state != UNUSED && p->parent == parent) {

      int child_index = ptree_buf.count++;
      if(child_index >= NPROC){
        release(&p->lock);
        return;
      }

      struct proc_info *cinfo = &ptree_buf.procs[child_index];
      safestrcpy(cinfo->name, p->name, sizeof(cinfo->name));
      cinfo->pid = p->pid;
      cinfo->child_count = 0;

      struct proc_info *pinfo = &ptree_buf.procs[parent_index];
      if(pinfo->child_count < NPROC)
        pinfo->children[pinfo->child_count++] = child_index;

      release(&p->lock);

      // بازگشتی برای نوه‌ها
      build_children(p, child_index);
    } else {
      release(&p->lock);
    }
  }
}

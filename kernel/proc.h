#pragma once


struct proc_info;
struct proc_tree;
#include "spinlock.h"
#include "ptree_struct.h"

void create_kernel_process(const char *name, void (*fn)(void));

struct mount_namespace {
  int refcount;
  struct inode *root;
};

struct ipc_namespace {
  int refcount;
};

struct pid_namespace {
  struct spinlock lock;
  int refcount;
  int nextpid;
};



// Saved registers for kernel context switches.
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state.
struct cpu {
  struct proc *proc;
  struct context context;
  int noff;
  int intena;
};

extern struct cpu cpus[NCPU];;

// Trapframe (unchanged)
struct trapframe {
  uint64 kernel_satp;
  uint64 kernel_sp;
  uint64 kernel_trap;
  uint64 epc;
  uint64 kernel_hartid;
  uint64 ra;
  uint64 sp;
  uint64 gp;
  uint64 tp;
  uint64 t0;
  uint64 t1;
  uint64 t2;
  uint64 s0;
  uint64 s1;
  uint64 a0;
  uint64 a1;
  uint64 a2;
  uint64 a3;
  uint64 a4;
  uint64 a5;
  uint64 a6;
  uint64 a7;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
  uint64 t3;
  uint64 t4;
  uint64 t5;
  uint64 t6;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// =====================
// Namespace structures
// =====================


struct uts_namespace {
  char nodename[64];
};


// Per-process state
struct proc {
  struct spinlock lock;
  // p->lock must be held
  enum procstate state;
  void *chan;
  int killed;
  int xstate;

  int pid;
  int global_pid;
  // parent
  struct proc *parent;

  // private
  uint64 kstack;
  uint64 sz;
  pagetable_t pagetable;
  struct trapframe *trapframe;
  struct context context;
  struct file *ofile[NOFILE];
  struct inode *cwd;
  char name[16];

  // =====================
  // Phase 3 additions
  // =====================

  // trace syscall
  int trace_mask;

  // namespaces
  struct pid_namespace   *pid_ns;
  struct uts_namespace   *uts_ns;
  struct mount_namespace *mnt_ns;
  struct ipc_namespace   *ipc_ns;
  struct pid_namespace *pending_pid_ns;



  // =====================
  // CFS scheduling fields
  // =====================
  int nice;
  int weight;
  uint64 vruntime;
  uint64 last_run_tick;
  uint64 slice_start_tick;
  uint64 slice_ticks;
};

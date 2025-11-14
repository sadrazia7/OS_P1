#ifndef PTREE_STRUCT_H
#define PTREE_STRUCT_H

#include "kernel/param.h"  // برای NPROC

struct proc_info {
  char name[16];
  int pid;
  int child_count;
  int children[NPROC];   // ایندکس بچه‌ها در procs
};

struct proc_tree {
  int count;
  struct proc_info procs[NPROC];
  int root_index;        // ایندکس ریشه در procs
};

#endif

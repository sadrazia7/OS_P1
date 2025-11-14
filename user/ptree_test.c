#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "ptree.h"

struct proc_tree pt;

void print_tree(struct proc_tree *pt, int idx, char *prefix, int islast);

int
main(int argc, char *argv[])
{
  int pid = 1;
  if(argc > 1)
    pid = atoi(argv[1]);

  if(ptree(pid, &pt) < 0){
    printf("ptree failed\n");
    exit(1);
  }

  print_tree(&pt, pt.root_index, "", 1);
  exit(0);
}

void
print_tree(struct proc_tree *pt, int idx, char *prefix, int islast)
{
  struct proc_info *p = &pt->procs[idx];

  if(prefix[0] == '\0')
    printf("PID: %d\n", p->pid);
  else
    printf("%s-- PID: %d\n", prefix, p->pid);

  char newprefix[128];
  int len = strlen(prefix);
  for(int i = 0; i < len; i++)
    newprefix[i] = prefix[i];

  newprefix[len]   = ' ';
  newprefix[len+1] = ' ';
  newprefix[len+2] = 0;

  for(int i = 0; i < p->child_count; i++){
    int cidx = p->children[i];
    print_tree(pt, cidx, newprefix, i == p->child_count - 1);
  }
}

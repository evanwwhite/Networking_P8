#include "libc.h"

volatile int x = 67;

int did_fork_fail(int pid){
  if(pid < 0){
    printf("%s", "*** fork failed\n");
    return 1;
  }
  return 0;
}

int main() {
    int pid = fork();

    if(did_fork_fail(pid)){ return 1; }

    int is_child = (pid == 0);

    if (is_child) {
        if(x != 67){
          printf("%s", "*** child saw wrong initial x\n");
          exit(2);
        }
        
        x = 42;

        printf("%s", "*** START child\n");
        int child_pid = fork();

        if(did_fork_fail(child_pid)){ exit(3); }

        int is_grandchild = (child_pid == 0);

        if(is_grandchild){
          if(x != 42){
            printf("%s", "*** grandchild saw wrong x\n");
            exit(4);
          }
          printf("%s", "*** grandchild\n");
          exit(42); 
        }

        int child_status = 0;
        waitpid(child_pid, &child_status, 0);
        printf("%s", "*** END child\n");
        
        
        exit(0);
    }

    //parent return
    int status = 0;
    waitpid(pid, &status, 0);

    if(x != 67){
      printf("%s", "*** parent saw wrong x\n");
      return 6;
    }
    printf("%s", "*** parent\n");
    return 0;
}

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int parent_to_child[2];
  int child_to_parent[2];
  int pid;
  char byte = 'x';

  (void)argc;
  (void)argv;

  if(pipe(parent_to_child) < 0){
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }

  if(pipe(child_to_parent) < 0){
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }

  pid = fork();

  if(pid < 0){
    fprintf(2, "pingpong: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    /*
       子进程：
       只读取 parent_to_child，
       只写入 child_to_parent。
     */
    close(parent_to_child[1]);
    close(child_to_parent[0]);

    if(read(parent_to_child[0], &byte, 1) != 1){
      fprintf(2, "pingpong: child read failed\n");
      exit(1);
    }

    printf("%d: received ping\n", getpid());

    if(write(child_to_parent[1], &byte, 1) != 1){
      fprintf(2, "pingpong: child write failed\n");
      exit(1);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);

    exit(0);
  }

  /*
     父进程：
     只写入 parent_to_child，
     只读取 child_to_parent。
   */
  close(parent_to_child[0]);
  close(child_to_parent[1]);

  if(write(parent_to_child[1], &byte, 1) != 1){
    fprintf(2, "pingpong: parent write failed\n");
    exit(1);
  }

  close(parent_to_child[1]);

  if(read(child_to_parent[0], &byte, 1) != 1){
    fprintf(2, "pingpong: parent read failed\n");
    exit(1);
  }

  printf("%d: received pong\n", getpid());

  close(child_to_parent[0]);

  wait(0);
  exit(0);
}
#include "kernel/types.h"
#include "user/user.h"

/*
 * 每调用一次 sieve，就对应素数筛中的一级进程。
 *
 * input_fd 是从左侧进程接收整数的管道读端。
 */
static void
sieve(int input_fd)
{
  int prime;
  int number;
  int next_pipe[2];
  int pid;

  /*
   * 管道中的第一个数字一定是当前阶段的素数。
   *
   * 当左侧所有写端都关闭后，read 返回 0，
   * 表示整条筛选链已经结束。
   */
  if(read(input_fd, &prime, sizeof(prime)) != sizeof(prime)){
    close(input_fd);
    exit(0);
  }

  printf("prime %d\n", prime);

  if(pipe(next_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    close(input_fd);
    exit(1);
  }

  pid = fork();

  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    close(input_fd);
    close(next_pipe[0]);
    close(next_pipe[1]);
    exit(1);
  }

  if(pid == 0){
    /*
     * 子进程负责下一级筛选。
     */
    close(next_pipe[1]);
    close(input_fd);

    sieve(next_pipe[0]);

    exit(0);
  }

  /*
   * 当前进程读取左侧管道中的剩余数字，
   * 将不能被当前 prime 整除的数字写入右侧管道。
   */
  close(next_pipe[0]);

  while(read(input_fd, &number, sizeof(number)) == sizeof(number)){
    if(number % prime != 0){
      if(write(next_pipe[1], &number, sizeof(number)) != sizeof(number)){
        fprintf(2, "primes: write failed\n");
        close(input_fd);
        close(next_pipe[1]);
        exit(1);
      }
    }
  }

  /*
   * 关闭右侧管道写端非常重要。
   * 这样下一级进程最终才能读取到 EOF。
   */
  close(input_fd);
  close(next_pipe[1]);

  /*
   * 等待下一级进程。
   *
   * 下一级又会等待它的子进程，因此最终会等待
   * 整条进程链全部退出。
   */
  wait(0);

  exit(0);
}

int
main(int argc, char *argv[])
{
  int first_pipe[2];
  int pid;
  int number;

  (void)argc;
  (void)argv;

  if(pipe(first_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  pid = fork();

  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    /*
     * 子进程开始第一层筛选。
     */
    close(first_pipe[1]);

    sieve(first_pipe[0]);

    exit(0);
  }

  /*
   * 父进程生成 2 到 35。
   */
  close(first_pipe[0]);

  for(number = 2; number <= 35; number++){
    if(write(first_pipe[1], &number, sizeof(number)) != sizeof(number)){
      fprintf(2, "primes: write failed\n");
      close(first_pipe[1]);
      exit(1);
    }
  }

  /*
   * 父进程写完后必须关闭写端。
   * 否则筛选进程不会收到 EOF。
   */
  close(first_pipe[1]);

  wait(0);
  exit(0);
}
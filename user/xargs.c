#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

/*
 * 使用输入的一行补充原有命令参数，
 * 然后 fork + exec 执行一次命令。
 */
static void
run_line(char *line, int length, int argc, char *argv[])
{
  char *exec_argv[MAXARG];
  int argument_count;
  int base_count;
  int index;
  int pid;

  argument_count = 0;

  /*
   * argv[0] 是 xargs 自身。
   *
   * argv[1] 开始才是要执行的命令及其原始参数。
   *
   * 例如：
   * xargs echo bye
   *
   * exec_argv 初始为：
   *   echo
   *   bye
   */
  for(index = 1; index < argc; index++){
    if(argument_count >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      exit(1);
    }

    exec_argv[argument_count++] = argv[index];
  }

  base_count = argument_count;
  index = 0;

  /*
   * 把输入行按照空格和制表符拆成参数。
   */
  while(index < length){
    /*
     * 跳过连续空白字符。
     */
    while(index < length &&
          (line[index] == ' ' || line[index] == '\t')){
      index++;
    }

    if(index >= length)
      break;

    if(argument_count >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      exit(1);
    }

    /*
     * 当前非空白字符是一个新参数的起点。
     */
    exec_argv[argument_count++] = &line[index];

    /*
     * 找到当前参数的结尾。
     */
    while(index < length &&
          line[index] != ' ' &&
          line[index] != '\t'){
      index++;
    }

    /*
     * 用 '\0' 将参数彼此分开。
     */
    if(index < length){
      line[index] = '\0';
      index++;
    }
  }

  /*
   * 空行不执行命令。
   */
  if(argument_count == base_count)
    return;

  exec_argv[argument_count] = 0;

  pid = fork();

  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    exec(exec_argv[0], exec_argv);

    /*
     * exec 成功时不会返回。
     */
    fprintf(2, "xargs: exec %s failed\n", exec_argv[0]);
    exit(1);
  }

  /*
   * 按顺序执行每一行。
   */
  wait(0);
}

int
main(int argc, char *argv[])
{
  char line[512];
  char character;
  int length;

  if(argc < 2){
    fprintf(2, "usage: xargs command [arguments ...]\n");
    exit(1);
  }

  if(argc >= MAXARG){
    fprintf(2, "xargs: too many initial arguments\n");
    exit(1);
  }

  length = 0;

  /*
   * 每次从标准输入读取一个字符。
   */
  while(read(0, &character, 1) == 1){
    if(character == '\n'){
      line[length] = '\0';

      run_line(line, length, argc, argv);

      length = 0;
      continue;
    }

    if(length >= (int)sizeof(line) - 1){
      fprintf(2, "xargs: input line too long\n");
      exit(1);
    }

    line[length++] = character;
  }

  /*
   * 处理没有以换行符结尾的最后一行。
   */
  if(length > 0){
    line[length] = '\0';
    run_line(line, length, argc, argv);
  }

  exit(0);
}
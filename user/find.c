#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

/*
 * 判断 path 最后一个路径分量是否等于 target。
 *
 * 例如：
 * path   = "./a/b"
 * target = "b"
 */
static int
path_name_equals(char *path, char *target)
{
  char name[DIRSIZ + 1];
  char *start;
  char *end;
  int length;

  end = path + strlen(path);

  /*
   * 跳过路径末尾可能存在的 '/'。
   */
  while(end > path && *(end - 1) == '/')
    end--;

  start = end;

  /*
   * 向前寻找最后一个 '/'。
   */
  while(start > path && *(start - 1) != '/')
    start--;

  length = end - start;

  if(length > DIRSIZ)
    return 0;

  memmove(name, start, length);
  name[length] = '\0';

  return strcmp(name, target) == 0;
}

static void
find(char *path, char *target)
{
  char buffer[512];
  char entry_name[DIRSIZ + 1];
  char *next;
  int fd;
  struct stat st;
  struct dirent de;

  fd = open(path, O_RDONLY);

  if(fd < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  /*
   * 当前路径自身的名字符合时，输出完整路径。
   */
  if(path_name_equals(path, target))
    printf("%s\n", path);

  /*
   * 普通文件和设备文件不需要继续递归。
   */
  if(st.type != T_DIR){
    close(fd);
    return;
  }

  /*
   * 给原路径、斜杠、最长文件名和 '\0' 留出空间。
   */
  if(strlen(path) + 1 + DIRSIZ + 1 > (int)sizeof(buffer)){
    fprintf(2, "find: path too long: %s\n", path);
    close(fd);
    return;
  }

  strcpy(buffer, path);
  next = buffer + strlen(buffer);

  /*
   * 避免出现重复斜杠。
   */
  if(next > buffer && *(next - 1) != '/')
    *next++ = '/';

  /*
   * xv6 中目录本身也是一个文件。
   * 每次 read 读取一个 struct dirent。
   */
  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;

    /*
     * de.name 是固定长度数组，不保证末尾有 '\0'。
     */
    memmove(entry_name, de.name, DIRSIZ);
    entry_name[DIRSIZ] = '\0';

    /*
     * 跳过 "." 和 ".."，否则会产生无限递归。
     */
    if(strcmp(entry_name, ".") == 0 ||
       strcmp(entry_name, "..") == 0){
      continue;
    }

    /*
     * 拼出子路径，例如：
     *
     * "." + "/" + "a" = "./a"
     */
    memmove(next, de.name, DIRSIZ);
    next[DIRSIZ] = '\0';

    find(buffer, target);
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: find path filename\n");
    exit(1);
  }

  find(argv[1], argv[2]);

  exit(0);
}
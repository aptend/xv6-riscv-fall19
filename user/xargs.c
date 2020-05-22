// Simple grep.  Only supports ^ . * $ operators.

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"


int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "usage: xargs <cmd> [cmd_args...]\n");
    exit();
  }

  // skip 'xargs'
  int origin_arg_count = argc - 1;
  argv = argv + 1;

  if (MAXARG - origin_arg_count <= 0) {
    fprintf(2, "xargs: too much arguments\n");
    exit();
  }
  char *cmd = argv[0];
  char *cmd_args[MAXARG];

  for(int i = 0; i < MAXARG; i++) {
    if (i < origin_arg_count) {
      cmd_args[i] = argv[i];
    }
    else {
      cmd_args[i] = 0; // set null pointer;
    }
  }

  char buf[512];
  int n, buf_len;
  char *p, *q;

  buf_len = 0;
  while((n = read(0, buf+buf_len, sizeof(buf)-buf_len-1)) > 0){
    buf_len += n;
    buf[buf_len] = '\0';
    p = buf;
    while((q = strchr(p, '\n')) != 0){
      *q = 0;
      // [p..q] is the appended argument
      cmd_args[origin_arg_count] = p;
      if (fork() == 0) {
        exec(cmd, cmd_args);
      } 
      while (wait() > 0);
      p = q+1;
    }
    if(buf_len > 0){
      buf_len -= p - buf;
      memmove(buf, p, buf_len);
    }
  }

  if (buf_len == sizeof(buf) - 1) {
    fprintf(2, "xargs: argument too long\n");
    exit();
  }

  exit();
}


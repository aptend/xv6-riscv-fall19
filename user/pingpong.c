#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int lpipe[2], rpipe[2];
    if (pipe(lpipe) < 0 || pipe(rpipe) < 0) {
      fprintf(2, "failed to create pipe");
      exit();
    }

    char buf[100];
    if (fork() == 0) {
      // child
      read(rpipe[0], &buf, 4);
      printf("%d: received %s\n", getpid(), buf);
      write(lpipe[1], "pong", 4);
      exit();
    } else {
      // parent
      write(rpipe[1], "ping", 4);
      read(lpipe[0], &buf, 4);
      printf("%d: received %s\n", getpid(), buf);
    }
    exit();
}

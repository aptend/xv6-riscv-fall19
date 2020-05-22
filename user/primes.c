#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void pipline(int recv_fd, int send_fd)
{
  close(send_fd);
  int p, n;
  int piped = 0;
  int right_pipe[2];

  if (read(recv_fd, &p, sizeof(int)) <= 0)
  {
    fprintf(2, "pipeline exit early\n");
    exit();
  };
  printf("prime %d\n", p);
  while (1)
  {
    if (read(recv_fd, &n, sizeof(int)) <= 0)
      break;
    if (n % p == 0)
      continue;
    if (piped == 0)
    {
      pipe(right_pipe);
      piped = 1;
      if (fork() == 0)
      {
        pipline(right_pipe[0], right_pipe[1]);
      }
      close(right_pipe[0]);
    }
    write(right_pipe[1], &n, sizeof(int));
  }

  close(recv_fd);
  if (piped == 1) {
    close(right_pipe[1]);
    while (wait() > 0);
  }
  exit();
}

int main(int argc, char *argv[])
{
  int rpipe[2];
  pipe(rpipe);
  if (fork() == 0) {
    pipline(rpipe[0], rpipe[1]);
  } else {
    close(rpipe[0]);
    int i;
    for(i = 2; i < 35; i++)
      write(rpipe[1], &i, sizeof(int));
  }
  close(rpipe[1]);
  while (wait() > 0);
  exit();
}

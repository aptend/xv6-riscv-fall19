#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MAXCMD 10
#define MAXARG 10
#define MAXLEN 1024
#define NULL ((void *)0)

int Fork() {
    int ret = fork();
    if (ret < 0) {
        fprintf(2, "nsh: can't fork\n");
        exit(1);
    }
    return ret;
}

int Pipe(int p[]) {
    int ret = pipe(p);
    if (ret < 0) {
        fprintf(2, "nsh: can't pipe\n");
        exit(1);
    }
    return ret;
}


typedef struct cmd_t {
    char *cmd;
    int argc;
    char *argv[MAXARG];
    int in_fd;
    int out_fd;
} cmd_t;

void cmd_init(cmd_t *c) {
    c->cmd = NULL;
    c->argc = 1;
    for (int i=0; i < MAXARG; i++)
      c->argv[i] = NULL;
    c->in_fd = -1;
    c->out_fd = -1;
}

void cmd_reset(cmd_t *c) {
  cmd_init(c);
}

void cmd_set_cmd(cmd_t *c, char *cmd) {
  c->cmd = cmd;
  c->argv[0] = cmd;
}

void cmd_append_arg(cmd_t *c, char *arg) {
  if (c->argc >= MAXARG) {
    fprintf(2, "nsh: too much arguments\n");
    exit(1);
  }
  c->argv[c->argc++] = arg;
}

void cmd_run(cmd_t *c) {
  if (Fork() == 0) {
    if (c->in_fd > 0) {
      close(0);
      dup(c->in_fd);
    }

    if (c->out_fd > 0) {
      close(1);
      dup(c->out_fd);
    }

    exec(c->cmd, c->argv);

  } else {
    if (c->in_fd > 0)
      close(c->in_fd);
    if (c->out_fd > 0)
      close(c->out_fd);
    wait(NULL);
  }
}


char cmd_buf[MAXLEN];

void handle_cmd_buf() {
  cmd_t current_cmd;
  cmd_t *cmd_p = &current_cmd;
  cmd_init(cmd_p);
  cmd_set_cmd(cmd_p, "echo");
  cmd_append_arg(cmd_p, "hi there");
  cmd_run(cmd_p);
  printf("%s\n", cmd_buf);
}



int main(int argc, char *argv[]) {
    while (1) {
        write(1, "@ ", 2);
        gets(cmd_buf, MAXLEN);
        if (cmd_buf[0] == '\0')
          break;
        
        char *p;
        p = strchr(cmd_buf, '\n');
        if (p == 0) {
          fprintf(2, "nsh: cmd too long\n");
          exit(1);
        } else {
          *p = '\0';
        }

        handle_cmd_buf();
    }
    exit(0);
}
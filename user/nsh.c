#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MAX_TOKEN_LEN 32
#define MAXARG 10
#define MAXLEN 1024
#define NULL ((void *)0)

#define true (1)
#define false (0)
typedef int bool;

int Open(const char *f, int perm)
{
  int ret = open(f, perm);
  if (ret < 0)
  {
    fprintf(2, "nsh: can't open %s\n", f);
    exit(1);
  }
  return ret;
}

int Fork()
{
  int ret = fork();
  if (ret < 0)
  {
    fprintf(2, "nsh: can't fork\n");
    exit(1);
  }
  return ret;
}

void Pipe(int p[])
{
  if (pipe(p) < 0)
  {
    fprintf(2, "nsh: can't pipe\n");
    exit(1);
  }
  return;
}

typedef struct cmd_t
{
  char *cmd;
  int argc;
  char *argv[MAXARG];
  int in_fd;
  int out_fd;
} cmd_t;

void cmd_init(cmd_t *c)
{
  c->cmd = NULL;
  c->argc = 1;
  for (int i = 0; i < MAXARG; i++)
    c->argv[i] = NULL;
  c->in_fd = -1;
  c->out_fd = -1;
}

void cmd_reset(cmd_t *c)
{
  cmd_init(c);
}

void cmd_set_cmd(cmd_t *c, char *cmd)
{
  c->cmd = cmd;
  c->argv[0] = cmd;
}

void cmd_append_arg(cmd_t *c, char *arg)
{
  if (c->argc >= MAXARG)
  {
    fprintf(2, "nsh: too much arguments\n");
    exit(1);
  }
  c->argv[c->argc++] = arg;
}

void cmd_run(cmd_t *c)
{
  if (c->cmd == NULL)
  {
    printf("NULL cmd, return\n");
    return;
  }
  if (Fork() == 0)
  {
    if (c->in_fd > 0)
    {
      close(0);
      dup(c->in_fd);
      close(c->in_fd);
    }

    if (c->out_fd > 0)
    {
      close(1);
      dup(c->out_fd);
      close(c->in_fd);
    }
    //fprintf(2, "run '%s'\n", c->cmd);
    exec(c->cmd, c->argv);
  }
  else
  {
    if (c->in_fd > 0)
      close(c->in_fd);
    if (c->out_fd > 0)
      close(c->out_fd);
    wait(NULL);
  }
}

char cmd_buf[MAXLEN];

char cmd_name_buf[MAX_TOKEN_LEN];

char cmd_argv_buf[MAXARG][MAX_TOKEN_LEN];
int argv_pos = 0;

bool is_cmd_set = false;

cmd_t current_cmd;
cmd_t *cmd_p = &current_cmd;

char token[MAX_TOKEN_LEN];
char *token_p = token;

void reset_for_next_cmd()
{
  cmd_init(cmd_p);
  token[0] = '\0';
  argv_pos = 0;
  is_cmd_set = false;
}

void handle_token()
{
  *token_p = '\0';
  if (token[0] == '\0')
    return;
  //printf("[debug]: handle token: %s\n", token);
  if (is_cmd_set)
  {
    // copy chars, including the trailing zero
    memmove(cmd_argv_buf[argv_pos], token, strlen(token) + 1);
    cmd_append_arg(cmd_p, cmd_argv_buf[argv_pos]);
    argv_pos++;
  }
  else
  {
    is_cmd_set = true;
    memmove(cmd_name_buf, token, strlen(token) + 1);
    cmd_set_cmd(cmd_p, cmd_name_buf);
  }
  token_p = token;
}

void handle_cmd_buf()
{
  char *ch = cmd_buf;
  int *pipe;
  int pipe0[2];
  int pipe1[2];
  int pipe_turn = 0;
  while (*ch != '\0')
  {
    switch (*ch)
    {
    case ' ':
      handle_token();
      break;
    case '>':
      handle_token();
      while (*(++ch) == ' ')
        ;
      while (*ch != ' ' && *ch != '|' && *ch != '>' && *ch != '<' && *ch != 0)
        *token_p++ = *ch++;
      *token_p = '\0';
      cmd_p->out_fd = Open(token, O_CREATE | O_WRONLY);
      token_p = token;
      ch--;
      break;
    case '<':
      handle_token();
      while (*(++ch) == ' ')
        ;
      while (*ch != ' ' && *ch != '|' && *ch != '>' && *ch != '<' && *ch != 0)
        *token_p++ = *ch++;
      *token_p = '\0';
      cmd_p->in_fd = Open(token, O_RDONLY);
      token_p = token;
      ch--;
      break;
    case '|':
      handle_token();
      if (pipe_turn == 0)
      {
        Pipe(pipe0);
        pipe = pipe0;
        pipe_turn = 1;
      }
      else
      {
        Pipe(pipe1);
        pipe = pipe1;
        pipe_turn = 0;
      }
      cmd_p->out_fd = pipe[1];
      cmd_run(cmd_p);
      reset_for_next_cmd();
      cmd_p->in_fd = pipe[0];
      break;
    default:
      *token_p++ = *ch;
      break;
    }
    ch++;
  }
  handle_token();
  cmd_run(cmd_p);
}

int main(int argc, char *argv[])
{
  while (1)
  {
    write(1, "@ ", 2);
    gets(cmd_buf, MAXLEN);
    if (cmd_buf[0] == '\0')
      break;

    char *p;
    p = strchr(cmd_buf, '\n');
    if (p == 0)
    {
      fprintf(2, "nsh: cmd too long\n");
      exit(1);
    }
    else
    {
      *p = '\0';
    }

    reset_for_next_cmd();
    handle_cmd_buf();
  }
  exit(0);
}

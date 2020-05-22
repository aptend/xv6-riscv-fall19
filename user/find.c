// Simple grep.  Only supports ^ . * $ operators.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

int match(char*, char*);

void
find(char *prefix, char *re)
{
  char buf[512], *p;
  struct dirent de;
  struct stat st; 
  int dir_fd;
  if((dir_fd = open(prefix, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", prefix);
    return;
  }
  if(fstat(dir_fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", prefix);
    close(dir_fd);
    return;
  }
  if (st.type != T_DIR) {
    fprintf(2, "find: %s is not a directory", prefix);
    close(dir_fd);
    return;
  }
  //              '/' + entry name + '\0'   
  if(strlen(prefix) + 1 + DIRSIZ + 1 > sizeof(buf)){
    printf("find: path is too long, stop searching\n");
    close(dir_fd);
    return;
  }

  strcpy(buf, prefix);
  p = buf + strlen(buf);
  *p++ = '/';
  while(read(dir_fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;
    if (de.name[0] == '.' && de.name[1] == 0)
      continue;
    if (de.name[0] == '.' && de.name[1] == '.' && de.name[2] == 0)
      continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;
    if(stat(buf, &st) < 0){
      printf("find: cannot stat %s\n", buf);
      continue;
    }
    switch (st.type) {
      case T_FILE:
        if (match(re, p)) {
          printf("%s\n", buf);
        }
        break;
      case T_DIR:
        find(buf, re);
        break;
    }
  }
  close(dir_fd);
}

int
main(int argc, char *argv[])
{
  if(argc < 3){
    fprintf(2, "usage: find <dir> <file_pattern>\n");
    exit();
  }

  find(argv[1], argv[2]);
  exit();
}

// Regexp matcher from Kernighan & Pike,
// The Practice of Programming, Chapter 9.

int matchhere(char*, char*);
int matchstar(int, char*, char*);

int
match(char *re, char *text)
{
  if(re[0] == '^')
    return matchhere(re+1, text);
  do{  // must look at empty string
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');
  return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text)
{
  if(re[0] == '\0')
    return 1;
  if(re[1] == '*')
    return matchstar(re[0], re+2, text);
  if(re[0] == '$' && re[1] == '\0')
    return *text == '\0';
  if(*text!='\0' && (re[0]=='.' || re[0]==*text))
    return matchhere(re+1, text+1);
  return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text)
{
  do{  // a * matches zero or more instances
    if(matchhere(re, text))
      return 1;
  }while(*text!='\0' && (*text++==c || c=='.'));
  return 0;
}


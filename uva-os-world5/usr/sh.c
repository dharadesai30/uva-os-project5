// Shell.
// fxl: quite simple. run an input loop, read input line, fork, 
//          parse into several major cmd types (exec, redir, pipe...)
//      and exec 
#include "user.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5   // exec cmd in bkgnd 

#define MAXARGS 10

// fxl: generic cmd type, will be cast into the following XXXcmd structs
struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);
void runcmd(struct cmd*) __attribute__((noreturn));

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:  // fxl: exec cmd in backgnd
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

int
getcmd(char *buf, int nbuf) // get a cmd line from stdin
{
  write(2, "$ ", 2);
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

// get a cmd line from string "cmds"
// "cmds": a string containing multiple commands, separate by \n. the last cmd
// may not end with \n
// return: the new cmd pointer in "cmds", or 0 if no more cmd or error
char *getcmd0(char *buf, int nbuf, const char *cmds) {
    if (!cmds || !buf || nbuf <= 0) return 0;

    const char *line_start = cmds;
    while (1) {
        // Skip leading whitespaces
        while (*line_start == ' ' || *line_start == '\t' || *line_start == '\r' || *line_start == '\n') {
            line_start++;
        }

        // If end of string, return 0 (no more commands)
        if (*line_start == '\0') {
            return 0;
        }

        // Check if the line is a comment
        if (*line_start == '#') {
            // Move to the next line
            const char *next_line = strchr(line_start, '\n');
            if (!next_line) {
                return 0; // No more lines
            }
            line_start = next_line + 1;
        } else {
            break; // Found a non-comment, non-whitespace line
        }
    }

    // Find the end of the current command (line)
    const char *line_end = strchr(line_start, '\n');
    if (!line_end) {
        line_end = line_start + strlen(line_start); // Last line without '\n'
    }

    // Adjust the end to remove trailing '\r' or '\n'
    const char *trim_end = line_end;
    while (trim_end > line_start && (trim_end[-1] == '\n' || trim_end[-1] == '\r')) {
        trim_end--;
    }

    int cmd_length = trim_end - line_start;
    if (cmd_length >= nbuf) {
        printf("Buffer too small for the command\n");
        return 0;
    }

    // Copy the command into the buffer
    memcpy(buf, line_start, cmd_length);
    buf[cmd_length] = '\0'; // Null-terminate the buffer

    // Return the pointer to the next command
    return (*line_end == '\n' || *line_end == '\0') ? (char *)(line_end + 1) : 0;
}


PROC_DEV_TABLE  // fcntl.h

// return 0 if ok
static int create_dev_procfs(void) {
  int fd; 

  // procfs 
  if (mkdir("/proc/") != 0) {
    printf("failed to create /proc"); 
    goto mkdev; 
  }
  for (int i = 0; i < sizeof(pdi)/sizeof(pdi[0]); i++) {
    struct proc_dev_info *p = pdi + i; 
    if (p->type != TYPE_PROCFS) continue; 
    if ((fd = open(p->path, O_RDONLY)) < 0 && (fd = open(p->path, O_CREATE)) < 0)
      printf("failed to create %s\n", p->path); 
    else 
      close(fd); 
  }

mkdev:    // devfs
  if (mkdir("/dev/") != 0) {
    printf("failed to create /dev"); 
    return -1; 
  }
  for (int i = 0; i < sizeof(pdi)/sizeof(pdi[0]); i++) {
    struct proc_dev_info *p = pdi + i; 
    if (p->type != TYPE_DEVFS) continue; 
    if ((fd = open(p->path, O_RDONLY)) < 0 
      && (fd = mknod(p->path, p->major, 0)) != 0) 
      printf("failed to create %s\n", p);  
    if (fd>0)
      close(fd); 
  }
  return 0; 
}

void logo() {
  char *args[] = {"cat", "logo.txt", 0}; 
  int pid=fork(); 
  if (pid==0) {
    if (exec("cat", args) <0)
      exit(1); 
  } 
  wait(0); 
}

#if 0   // just an example
void run_nes() {
  // char *args[] = {"nes", 0}; 
  char *args[] = {"nes", "kungfu.nes", 0}; 
  printf("auto launch nes...\n");
  int pid=fork(); 
  if (pid==0) {
    if (exec("nes", args) <0)
      exit(1); 
  } 
  wait(0); 
}
#endif 

/* read init cmd from a file. return # of bytes read. <0 on err*/
#define INITCMD_MAX 512
char init_cmds[INITCMD_MAX] = {0}; // max init cmd; 
int read_init_cmd() {
  // list of files to try
  const char *fnames[] = {
    "/d/initrc.txt",
    "/initrc.txt",
    "./initrc.txt",
    0
  };
  int fd = -1, n; 
  
  for (const char **fn = fnames; *fn; fn++) {
    if((fd = open(*fn, 0)) > 0) {
      printf("sh: found from %s. load cmds.\n", *fn);
      break; 
    }
  }
  if (fd<=0) return -1; 

  // read until returning 0
  char *buf=init_cmds; int len = INITCMD_MAX;
  while((n = read(fd, buf, len)) > 0) {
    buf += n; len -= n; 
    if (!len) return -2; // run out of buffer
  }
  return (INITCMD_MAX - len); 
}

int
main(void)
{
  static char buf[100];
  int fd;

  // printf("entering sh ....\n"); 

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    printf("open console, fd %d ....\n", fd); 
    if(fd >= 3){
      close(fd);
      break;
    }
  }
  
  if (mkdir("/tmp") != 0)
    printf("warning: failed to create /tmp"); 
  printf("sh: To create dev/procfs entries ...."); 
  if (create_dev_procfs()==0)
    printf("OK\n"); 
  logo();

  // Load & run a list of commands from "initrc.txt"  
  read_init_cmd(); 
  const char *p=init_cmds; 
  while((p=getcmd0(buf, sizeof(buf), p))) {
    printf("%s: exec init cmd: [%s]\n", __FILE__, buf);
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        fprintf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait(0);
    sleep(500);  // back-to-back exec() deadlock... (TBD)
  } 

  // Read and run input commands from stdin (inf loop)
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        fprintf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait(0);
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}

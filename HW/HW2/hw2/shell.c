#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <limits.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_exec(struct tokens *tokens);

int is_full_directory(char* path);
char *find_path(char* path, char *final_path );
bool redirect_stdin(struct tokens *tokens);
bool redirect_stdout(struct tokens *tokens);
/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "print the current directory"},
  {cmd_cd, "cd", "change directory"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(struct tokens *tokens) {
  tokens_destroy(tokens);
  exit(0);
}

/* Print current directory */
int cmd_pwd(unused struct tokens *tokens){
  char cwd[PATH_MAX];
  getcwd(cwd, sizeof(cwd));
  printf("Current working dir: %s\n", cwd);
  return 1;
}

/* Change directort */
int cmd_cd(struct tokens *tokens){
  int check = chdir(tokens_get_token(tokens,1));
  if(check == 0){
    cmd_pwd(tokens);
  }else{
    printf("Error Directory\n");
    return -1;
  }
  return 1;
}

/* Execute File */
int cmd_exec(struct tokens *tokens){ 
    char *final_path;
  if(is_full_directory(tokens_get_token(tokens,0)) == 0){
    final_path = find_path(tokens_get_token(tokens,0), final_path);
  }
  int res;
  pid_t pid = fork();
  if(pid == 0){
    setpgid(0, 0);
    tcsetpgrp(0, getpgrp());
		
    // Reset signal handlers
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    unused char *path;
    int len = tokens_get_length(tokens);
    if(redirect_stdin(tokens) || redirect_stdout(tokens)){
      len -=2;
    }

    if(final_path==NULL){
      path = tokens_get_token(tokens, 0);
    }else{
      path = final_path;
    }
    
  puts(final_path);
    unused char *args[len + 1];
    for(int i = 0;i < len;i++){
      args[i] = tokens_get_token(tokens, i);
    }
    args[len] = NULL;
    execv(path, args);
    exit(0);
  }else{
    wait(&res);
  }
  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Check if the agrument is full directory */
int is_full_directory(char* path){
  char *step = path;

  for(int i=0; i<sizeof(path)/sizeof(char); i++,step++){
    if(*step == '/'){
      return 1;
    }
  }
  return 0;
}

int isFileExistsAccess(const char *path)
{
// Check for file existence
  if (access(path, F_OK) == -1)
    return 0;
  return 1;
}
  
/* Find one accessable path */
char *find_path(char* path, char *final_path ){
  char paths[PATH_MAX];
  strcpy(paths, getenv("PATH"));
  for (int i=0; paths[i] != '\0'; i++){
    if(paths[i] == ':'){
      paths[i] = ' ';
    }
  }
  struct tokens* path_tokens = tokenize(paths);
  char slash[] = {'/', '\0'};
  char *end = strcat(slash, path);
  for (int i=0; i<tokens_get_length(path_tokens); i++){
    char *dir = tokens_get_token(path_tokens, i);
    char *possible_path = strcat(dir, end);
    if(isFileExistsAccess(possible_path)){
      
      return possible_path;
    }
  }
  return NULL;
}

/* Redirect STDIN if redirect in tokens */
bool redirect_stdin(struct tokens *tokens){
  int num_args = tokens_get_length(tokens);
  if (num_args >= 3 && strcmp(tokens_get_token(tokens, num_args-2), "<") == 0){
    int fd = open(tokens_get_token(tokens, num_args-1), O_RDONLY);
    dup2(fd, 0);
    close(fd);
    return true;
  }
  return false;
}

/* Redirect STDOUT if redirect in tokens */
bool redirect_stdout(struct tokens *tokens){
  int num_args = tokens_get_length(tokens);
  if(num_args >=3 && strcmp(tokens_get_token(tokens, num_args-2), ">") == 0){
    printf("Opening file %s.\n", tokens_get_token(tokens, num_args-1));
    int fd = open(tokens_get_token(tokens, num_args-1), O_RDWR|O_TRUNC|O_CREAT, 0777);
    dup2(fd, 1);
    close(fd);
    return true;
  }
  return false;
}

void executePrograms(char *line){
  int i = 0;
  int sep = 0;
  while(line[i] != '\0'){
      if(line[i] == '|'){
      sep = i;
      break;
      }
      i++;
  }
  if(sep == 0){
    struct tokens *tokens = tokenize(line);
    cmd_exec(tokens);
  }else{
    char before[4096];
    char after[4096];
    strncpy(before, line, sep-1);
    before[sep-1] = '\0';
    strcpy(after, line);

    int pipefd[2];
    pid_t p1;
   // if(pipe(pipefd) < 0) perrpr("pipe fail");
    p1 = fork();
    if(p1){
      close(pipefd[1]);
      dup2(pipefd[0],0);
      struct tokens *tokens = tokenize(after);
      cmd_exec(tokens);
    }else{
      close(pipefd[0]);
      dup2(pipefd[1],1);
      struct tokens *tokens = tokenize(before);
      cmd_exec(tokens);
    }
  }
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
//      /* REPLACE this to run commands as programs. */
	if(tokens_get_length(tokens) >= 2){
	  cmd_exec(tokens);
	}else {
	fprintf(stdout, "This shell doesn't know how to run programs.\n");
      }
    }
    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

// +1 to all maximums due to the possible \n at the end of each line
#define MAX_INPUT_LEN 1201
#define MAX_TOKENS 21
#define MAX_TOKEN_LEN 81

// defines a command
typedef struct s_command {
  char** params;      // Tokens
  int param_count;    // num tokens
  int bg;             // 0: not background process; 1: background process
  char* infile;       // input redirect file name
  char* outfile;      // output redirect file name
} command;

// breaks input string into tokens.  input string must be delimited by " "
int tokenize(char* input, char** tokens) {
  char* token = strtok(input, " ");
  int i = 0;
  while (token != NULL)
    {
      if ((tokens[i] = (char*) calloc(strlen(token) + 1, sizeof(char))) == NULL) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
      }
      strcpy(tokens[i], token);
      token = strtok(NULL, " ");
      i++;
    }
  tokens[i-1][strlen(tokens[i-1]) - 1] = '\0';
    
  // return token count
  return i;
}

// executes a command through child process
int execute(command commands, char* envp[], int in, int out) {
  pid_t pid;
  int status;
  
  switch(pid = fork()) {
    case -1:
      // fork error
      perror("Fork error\n");
      exit(EXIT_FAILURE);
    case 0:
      //child process
      // if redirect dup in to stdin
      if (in != 0) {
        close(0);
        if (dup2(in, 0) < 0) {
          perror("fd dup failed\n");
          exit(EXIT_FAILURE);
        }
        close(in);
      }

      // if redirect
      // dup out to stdout
      if (out != 1) {
        close(1);
        if (dup2(out, 1) < 0) {
          perror("fd dup failed\n");
          exit(EXIT_FAILURE);
        }
        close(out);
      }

      execvpe(commands.params[0], commands.params, envp);

      // execvpe should never return here
      perror("Child process failed\n");
      exit(EXIT_FAILURE);
    default:
      //parent process
      if (commands.bg == 0) waitpid(pid, &status, 0);
      return 0;
      
  }
};

// tests if command has an io redirect to file
int execute_redirect_helper(command commands, char* envp[],int in_fd, int out_fd) {

  if (commands.infile != NULL) {
    if ((in_fd = open(commands.infile, 0)) < 0) {
      fprintf(stderr, "Could not open file [%s]\n", commands.infile);
      exit(EXIT_FAILURE);
    }
  }

  if (commands.outfile != NULL) {
    if ((out_fd = open(commands.outfile, O_CREAT | O_WRONLY, 0666)) < 0) {
      fprintf(stderr, "Could not read or create file [%s]\n", commands.outfile);
      exit(EXIT_FAILURE);
    }
  }

  execute(commands, envp, in_fd, out_fd);

  if (commands.infile != NULL) {
    if (close(in_fd) < 0) {
      fprintf(stderr, "failed to close file descriptor [%d]\n", in_fd);
      exit(EXIT_FAILURE);
    }
  }
  if (commands.outfile != NULL) {
    if (close(out_fd) < 0) {
      fprintf(stderr, "failed to close file descriptor [%d]\n", out_fd);
      exit(EXIT_FAILURE);
    }
  }

  return 0;
}

// Executes cmd_count commands with pipes
int execute_pipes(command* commands, int cmd_count, char* envp[]) {

  int fd[2];
  int in_fd = 0;
  for (int i = 0; i < cmd_count - 1; i++)
  {
    pipe(fd);

    if (commands[i].bg == 1) {
      execute_redirect_helper(commands[i], envp, in_fd, 1);
      in_fd = 0;
    } else {
      execute_redirect_helper(commands[i], envp, in_fd, fd[1]);
      in_fd = fd[0];
    }


    close(fd[1]);

  }

  execute_redirect_helper(commands[cmd_count - 1], envp, in_fd, 1);

  return 0;
}

// Transfers tokens to a command
int transfer_tokens_cmd(command* commands, int cmd_count, int cmd_head, int cmd_tail, char** tokens) {
  if ((commands[cmd_count].params = (char**) calloc(cmd_head - cmd_tail + 1, sizeof(char*))) == NULL) {
    perror("Failed to allocate memory");
    exit(EXIT_FAILURE);
  }
  commands[cmd_count].param_count = cmd_head - cmd_tail + 1;

  for (int j = 0; j < cmd_head - cmd_tail; j++)
    {
      if ((commands[cmd_count].params[j] = (char*) calloc(strlen(tokens[j + cmd_tail]) + 1, sizeof(char))) == NULL) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
      }
      strcpy(commands[cmd_count].params[j], tokens[j + cmd_tail]);
    }
  commands[cmd_count].params[cmd_head - cmd_tail] = NULL;
  return 0;
}

// Parses tokens for pipes, &'s, and io redirects
int parse_tokens(command* commands, char** tokens, int token_count, int* multi_cmd) {
  int cmd_head, cmd_tail, in_redirect, out_redirect, in_command, cmd_count, has_pipe;
  cmd_head = cmd_tail = in_redirect = out_redirect = in_command = cmd_count = has_pipe = 0;

  // different from out_redirect.  used to check for ambiguous IO errors
  int out_redirect_error_checker = 0;
  int in_redirect_error_checker = 0;
  for (int i = 0; i < token_count; i++) {

    if (in_redirect == 1) {
      // in filename
      if ((commands[cmd_count - 1].infile = (char*) malloc(strlen(tokens[i]) + 1)) == NULL) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
      }
      strcpy(commands[cmd_count - 1].infile, tokens[i]);
      in_redirect = 0;
      in_command = 0;

    } else if (out_redirect == 1) {
      // out filename
      if((commands[cmd_count - 1].outfile = (char*) malloc(strlen(tokens[i]) + 1)) == NULL) {
        perror("Failed to allocate memory");
        exit(EXIT_FAILURE);
      }
      strcpy(commands[cmd_count - 1].outfile, tokens[i]);
      out_redirect = 0;
      in_command = 0;
      out_redirect_error_checker = 1;

    } else if (strcmp(tokens[i], "<") == 0) {

      if (in_redirect_error_checker == 1) {
        fprintf(stderr, "Invalid command: ambiguous IO [... cmd1 | cmd2 < file ...]\n");
        exit(EXIT_FAILURE);
      }

      // in redirect
      in_redirect = 1;
      in_command = 0;

    } else if (strcmp(tokens[i], ">") == 0) {

      // out redirect
      out_redirect = 1;
      in_command = 0;

    } else if (strcmp(tokens[i], "|") == 0) {
      // pipe

      if (out_redirect_error_checker) {
        fprintf(stderr, "Invalid command: ambiguous IO [... cmd1 > file | cmd2 ...]\n");
        exit(EXIT_FAILURE);
      }

      in_command = 0;
      transfer_tokens_cmd(commands, cmd_count - 1, cmd_head, cmd_tail, tokens);
      cmd_tail = i + 1;
      *multi_cmd = 1;
      in_redirect_error_checker = 1;

    } else if (strcmp(tokens[i], "&") == 0) {
      // background
      in_command = 0;
      transfer_tokens_cmd(commands, cmd_count - 1, cmd_head, cmd_tail, tokens);
      commands[cmd_count - 1].bg = 1;
      cmd_tail = i + 1;
      *multi_cmd = 1;

    } else {
      //command parameter

      if (in_command == 0) {
        cmd_count++;
        in_command = 1;
        cmd_tail = cmd_head = i;
        out_redirect_error_checker = 0;
      }
      cmd_head++;
    }
  }

  if (cmd_tail < cmd_head) {
    transfer_tokens_cmd(commands, cmd_count - 1, cmd_head, cmd_tail, tokens);
  }

  return cmd_count;
}

int main(int argc, char* argv[]) {

  char PATH[19] = "PATH=/bin:/usr/bin";   // execution search path
  char input[MAX_INPUT_LEN];              // input buffer
  char** tokens;                          // input tokens
  int token_count = 0;                    // num tokens
  int cmd_count;                          // num commands
  int multi_cmd;                          // 0: single command, 1: multiple commands
  command* commands;                      // array of commands

  bzero(input, MAX_INPUT_LEN);
  if ((tokens = (char**) calloc(MAX_TOKENS + 1, sizeof(char*))) == NULL) {
    perror("Failed to allocate memory");
    exit(EXIT_FAILURE);
  }
  if ((commands = (command*) calloc(MAX_TOKENS, sizeof(command))) == NULL) {
    perror("Failed to allocate memory");
    exit(EXIT_FAILURE);
  }
  static const command null_command;

  while (1)
    {
      // prompt
      printf("$ ");

      // get input
      fgets(input, sizeof(input), stdin);

      // check if input is within bounds
      if (strchr(input, '\n') == NULL) {
        fprintf(stderr, "Input too long\n");
        exit(EXIT_FAILURE);
      }

      cmd_count = multi_cmd = 0;

      // reset commands to NULL
      for (int i = 0; i < MAX_TOKENS; i++) {
        commands[i] = null_command;
      }

      // tokenize input
      token_count = tokenize(input, tokens); 

      // check for 'exit'
      if (strcmp(tokens[0], "exit") == 0) break;

      // Create environment variable
      char *envp[] = {PATH, NULL};

      // parse tokens
      cmd_count = parse_tokens(commands, tokens, token_count, &multi_cmd);

      // execute commands
      if (multi_cmd) {
        execute_pipes(commands, cmd_count, envp);
      } else {
        execute_redirect_helper(commands[0], envp, 0, 1);
      }

      // free commands
      for (int i = 0; i < cmd_count; i++) {
        for (int j = 0; j < commands[i].param_count; j++) {
          free(commands[i].params[j]);
        }
        if (commands[i].infile != NULL) free(commands[i].infile);
        if (commands[i].outfile != NULL) free (commands[i].outfile);
      }

    }

  // free commands array
  free(commands);

  // free tokens
  for (int i = 0; i < token_count; i++) 
    {
      free(tokens[i]);
    }
  free(tokens);
  
  return 0;
}

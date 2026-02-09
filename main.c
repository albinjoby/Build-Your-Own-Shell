#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>

struct termios orig_termios;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;

    raw.c_lflag &= ~(ECHO | ICANON);  // disable echo + canonical mode
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void parse_args(char *input, char **args){
    int i = 0, j = 0, argc = 0;
    int single_quoted = 0, double_quoted = 0, escaped = 0;
    char buffer[1024];

    while (input[i] != '\0') {
        // escape string literals
        if (input[i] == '\\' && !single_quoted) {
            i++;
            if (input[i] == '\0') {
                break;
            }
            if (double_quoted) {
                if (input[i] == '"' || input[i] == '\\' || input[i] == '$' || input[i] == '`')  {
                    buffer[j++] = input[i++];
                }else{
                    buffer[j++] = '\\';
                    buffer[j++] = input[i++];
                }
            }else{
                buffer[j++] = input[i++];
            }
            escaped = 1;
            continue;
        }
        // single qoutes
        if (input[i] == '\"' && !single_quoted) {
            double_quoted = !double_quoted;
            i++;
            continue;
        }
        // double qoutes
        if (input[i] == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
            i++;
            continue;
        }
        // split arguments
        if (input[i] == ' ' &&  !double_quoted && !single_quoted) {
            if (j > 0) {
                buffer[j] = '\0';
                args[argc++] = strdup(buffer);
                j = 0;
            }
            i++;
            escaped = 0;
            continue;
        }
        // normal character
        buffer[j++] = input[i++];
        escaped = 0;
    }
    // final argument
    if (j>0) {
        buffer[j] = '\0';
        args[argc++] = strdup(buffer);
    }
    args[argc] = NULL;
}

int handler_redirection(char **args, char **output_file){
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "1>") == 0 || strcmp(args[i], ">") == 0) {
            if(args[i + 1] == NULL) return -1;
            *output_file = args[i + 1];
            args[i] = NULL;
            return 1;
        }else if (strcmp(args[i], "2>") == 0) {
            if(args[i + 1] == NULL) return -1;
            *output_file = args[i + 1];
            args[i] = NULL;
            return 2;
        }else if (strcmp(args[i], ">>") == 0 || strcmp(args[i], "1>>") == 0) {
            if(args[i + 1] == NULL) return -1;
            *output_file = args[i + 1];
            args[i] = NULL;
            return  3;
        }else if (strcmp(args[i], "2>>") == 0) {
            if(args[i] == NULL) return -1;
            *output_file = args[i + 1];
            args[i] = NULL;
            return 4;
        }
    }
    return 0;
}

void autocomplete(char *command, char **commands, int *len, int command_count){
    for (int i = 0; i < command_count; i++) {
        if(strncmp(command, commands[i], *len) == 0){
            write(STDOUT_FILENO, commands[i] + *len, strlen(commands[i]) - *len);
            strcpy(command, commands[i]);
            *len = strlen(commands[i]);
            
            if (*len < 1023) {
                command[*len] = ' ';
                command[*len + 1] = '\0';
                (*len)++;
                write(STDOUT_FILENO, " ", 1);
            }
            return ;
        }
    }
    return;
}

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);
  char command[1024];
  char *commands[] = {"exit","echo","type","pwd","cd"};
  int len = 0;

  enable_raw_mode();
  while (1) {
      printf("$ ");
      len = 0;
     
      // get user input (with autocompletion) 
      while (1) {
          char c;
          read(STDIN_FILENO, &c, 1);
          if (c == '\t') {
              autocomplete(command, commands, &len, sizeof(commands)/sizeof(commands[0]));
              continue;
          }
          if (c == '\n' || c == '\r') {
              command[len] = '\0';
              printf("\n");
              break;
          }
          if (c == 127 || c == 8) {
              if (len > 0) {
                  len--;
                  command[len] = '\0';
                  write(STDOUT_FILENO, "\b \b", 3);
              }
              continue;
          }
          command[len++] = c;
          write(STDOUT_FILENO, &c, 1);
      }

      if (strcmp(command, "exit") == 0) {
          break;
      }else if (strcmp(command, "pwd") == 0) {
          char cwd[64];
          getcwd(cwd, sizeof(cwd));
          printf("%s\n",cwd);
      }else if (strncmp(command, "cd ", 3) == 0) {
         char *path = command+3;
         if (strcmp(path, "~") == 0) {
             chdir(getenv("HOME"));
         }else if (chdir(path) != 0){
             printf("cd: %s: No such file or directory\n",path);
         }
      }else if (strncmp(command, "echo ", 5) == 0) {
          char *string = command + 5;
          char *args[64];
          parse_args(string, args);
          
          char *output_file = NULL;
          int redirect_status = handler_redirection(args, &output_file);
          
          if (redirect_status == -1) {
              printf("echo: missing file operand\n");
              continue;
          }
          
          int saved_stdout = -1;
          int saved_stderr = -1;
          
          if (redirect_status > 0) {
              int flags = O_WRONLY | O_CREAT;
              if (redirect_status == 1 || redirect_status == 2) {
                  flags |= O_TRUNC;
              }else{
                  flags |= O_APPEND;
              }
              
              int fd = open(output_file, flags, 0644);
              if (fd < 0) {
                  perror("open");
                  exit(0);
              }
              
              if (redirect_status == 1 || redirect_status == 3) {
                saved_stdout = dup(STDOUT_FILENO);
                dup2(fd,STDOUT_FILENO);
              }else{
                  saved_stderr = dup(STDERR_FILENO);
                  dup2(fd, STDERR_FILENO);
              }              
              close(fd);
          }
          
          for (int i=0; args[i]!=NULL; i++) {
              if (i > 0) {
                  printf(" ");
              }
              printf("%s",args[i]);
          }
          printf("\n");
          
          if (redirect_status == 1 || redirect_status == 3) {
              dup2(saved_stdout, STDOUT_FILENO);
              close(saved_stdout);
          }else if(redirect_status == 2 || redirect_status == 4){
              dup2(saved_stderr, STDERR_FILENO);
              close(saved_stderr);
          }
      }else if (strncmp(command, "type ", 5) == 0) {
          int flag = 0;
          for (int i=0; i<sizeof(commands)/sizeof(commands[0]); i++) {
              if (strcmp(command+5, commands[i]) == 0) {
                  printf("%s is a shell builtin\n",command+5);
                  flag = 1;
                  break;
              }
          }
          if (!flag) {
            const char *env = getenv("PATH");
            char *path_copy = strdup(env);
            char *dir = strtok(path_copy, ":");
            char *cmd = command + 5;

            while (dir != NULL) {
                char fullpath[1024];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, cmd);

                if (access(fullpath, X_OK) == 0) {
                        printf("%s is %s\n", cmd, fullpath);
                        flag = 1;
                        break;
                    }

                    dir = strtok(NULL, ":");
                }

                free(path_copy);
            }

          if (!flag) {
              printf("%s: not found\n",command+5);
          }
      }else{
          char *args[64];
          parse_args(command, args);

          char *output_file = NULL;
          int redirect_status = handler_redirection(args, &output_file);
          
          if(redirect_status == -1){
              printf("missing file operand\n");
              continue;
          }
          
          pid_t pid = fork();
          
          if (pid == 0) {
              if(redirect_status > 0){
                  int flags = O_WRONLY | O_CREAT;
                  if (redirect_status == 1 || redirect_status == 2) {
                      flags |= O_TRUNC;
                  }else{
                      flags |= O_APPEND;
                  }
                  int fd = open(output_file, flags, 0644);
                  if (fd < 0) {
                      perror("open");
                      exit(1);
                  }
                  if (redirect_status == 1 || redirect_status == 3) {
                      dup2(fd, STDOUT_FILENO);
                  }else{
                      dup2(fd, STDERR_FILENO);
                  }
                  close(fd);
              }
              
              execvp(args[0], args);
              printf("%s: command not found\n", args[0]);
              exit(1);
          } else {
              waitpid(pid, NULL, 0);
          }
      }
  }
  return 0;
}

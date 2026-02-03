#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

void parse_args(char *input, char **args){
    int i = 0, j = 0, argc = 0;
    int quoted = 0;
    char buffer[1024];

    while (input[i] != '\0') {
        if (input[i] == '\'') {
            quoted = !quoted;
            i++;
            continue;
        }
        if (input[i] == ' ' && !quoted) {
            if (j > 0) {
                buffer[i] = '\0';
                args[argc++] = strdup(buffer);
                j = 0;
            }
            i++;
            continue;
        }
        buffer[j++] = input[i++];
    }
    if (j>0) {
        buffer[i] = '\0';
        args[argc++] = strdup(buffer);
    }
    args[argc] = NULL;
}

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);
  char command[100];
  char *commands[] = {"exit","echo","type","pwd","cd"};

  while (1) {
      printf("$ ");
      fgets(command,sizeof(command),stdin);
      command[strcspn(command, "\n")] = '\0';

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
          int single_quoted = 0, double_quoted = 0;
          int i = 0;
          while (string[i] != '\0') {
              if(string[i] == '\"'){
                  double_quoted = !double_quoted;
                  i++;
                  continue;
              }else if (string[i] == '\'') {
                  if (!double_quoted) {
                      single_quoted = !single_quoted;
                      i++;
                      continue;
                  }else{
                      printf("'");
                      i++;
                      continue;
                  }
              }
              if (string[i] == ' ') {
                  if (single_quoted || double_quoted) {
                      printf(" ");
                      i++;
                  }else{
                      while (string[i] == ' ') {
                          i++;
                      }
                      printf(" ");
                  }
                  continue;
              }else{
                  printf("%c",string[i]);
              }
              i++;
          }
          printf("\n");
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

          pid_t pid = fork();
          if (pid == 0) {
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);
  char command[100];
  char *commands[] = {
      "exit",
      "echo",
      "type",
      "pwd",
      "cd"
  };

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
      }
      else if (strncmp(command, "echo ", 5) == 0) {
          char *string = command + 5;
          int qouted = 0;
          int i = 0;
          while (string[i] != '\0') {
              if (string[i] == '\'') {
                  qouted = !qouted;
                  i++;
                  continue;
              }
              if (string[i] != ' ') {
                  printf("%c",string[i]);
              }else{
                  if (qouted) {
                      printf("%c",string[i]);
                  }else{
                      while (string[i] == ' ') {
                          i++;
                      }
                      printf(" ");
                      continue;
                  }
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
      }
      else{
          char *args[64];
          int argc = 0;

          char *token = strtok(command," ");
          while (token != NULL && argc < 63) {
              args[argc++] = token;
              token = strtok(NULL, " ");
          }
          args[argc] = NULL;

          pid_t pid = fork();

          if (pid == 0) {
              execvp(args[0], args);
              printf("%s: command not found\n",command);
              exit(1);
          }else{
              waitpid(pid, NULL, 0);
          }
      }
  }
  return 0;
}

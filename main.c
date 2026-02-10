#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>
#include <dirent.h>
#include <sys/stat.h>

char *commands[] = {"exit","echo","type","pwd","cd", NULL};
char *executables[10000];
int exe_idx = 0;
int tab_pressed_once = 0;

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

int cmp(const void *a, const void *b) {
    char *s1 = *(char **)a;
    char *s2 = *(char **)b;
    return strcmp(s1, s2);
}

int already_added(char **arr, int count, char *s){
    for (int i = 0; i < count; i++) {
        if (strcmp(arr[i], s) == 0) return 1;
    }
    return  0;
}

void autocomplete(char *command, int *len){
    char *predictions[1000];
    int pred_count = 0;
    
    for (int i = 0; commands[i] != NULL; i++) {
           if (strncmp(command, commands[i], *len) == 0) {
               if (!already_added(predictions, pred_count, commands[i])) {
                   predictions[pred_count++] = commands[i];
               }
           }
       }
    predictions[pred_count] = NULL;
    
    for (int i = 0; executables[i] != NULL; i++) {
        if (strncmp(command, executables[i], *len) == 0) {
            if (!already_added(predictions, pred_count, executables[i])) {
                predictions[pred_count++] = executables[i];
            }
        }
    }
    predictions[pred_count] = NULL;

    if (pred_count == 0) {
        write(STDOUT_FILENO, "\a", 1);
        tab_pressed_once = 0;
        return;
    }else if (pred_count == 1) {
        char *match = predictions[0];
        
        write(STDOUT_FILENO, match + *len, strlen(match) - *len);
        strcpy(command, match);
        *len = strlen(match);
        
        if (*len < 1023) {
            command[*len] = ' ';
            command[*len + 1] = '\0';
            (*len)++;
            write(STDOUT_FILENO, " ", 1);
        }
        tab_pressed_once = 0;
        return ;
    }else{
        qsort(predictions, pred_count, sizeof(char *), cmp);
        if (tab_pressed_once == 0) {
                write(STDOUT_FILENO, "\a", 1);
                tab_pressed_once = 1;
                return;
            }
        tab_pressed_once = 0;
        printf("\n");
        for (int i = 0; predictions[i] != NULL; i++){
            printf("%s",predictions[i]);
            if (i != pred_count - 1) printf("  ");
        }
        printf("\n");
    }
    printf("$ ");
    write(STDOUT_FILENO, command, *len);
    return;
}

void add_executable(char *name){
    if (exe_idx >= 9999) return;
    executables[exe_idx++] = strdup(name);
    executables[exe_idx] = NULL;
}

void get_executables(){
     char *path = getenv("PATH");
     char *path_copy = strdup(path);
     char *dir = strtok(path_copy, ":");
     while (dir != NULL) {
         DIR *d = opendir(dir);
         if (!d) {
             dir = strtok(NULL, ":");
             continue;
         }
         struct dirent *entry;
         while ((entry = readdir(d)) != NULL) {
             if (entry->d_name[0] == '.') continue;
             
             char fullpath[1024];
             snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, entry->d_name);
             
             // if (access(fullpath, X_OK) == 0) {
             //     add_executable(entry->d_name);
             // }
              struct stat st;
                if (stat(fullpath, &st) == 0 &&
                    S_ISREG(st.st_mode) &&
                    access(fullpath, X_OK) == 0) {
    
                    add_executable(entry->d_name);
                }
             
         }
         closedir(d);
         dir = strtok(NULL, ":");
     }
     free(path_copy);
}

int main(int argc, char *argv[]) {
  get_executables();
  setbuf(stdout, NULL);
  char command[1024];
  int len = 0;

  while (1) {
      
      if (isatty(STDIN_FILENO)) {
          enable_raw_mode();
          printf("$ ");
                len = 0;
               
                // get user input (with autocompletion) 
                while (1) {
                    char c;
                    read(STDIN_FILENO, &c, 1);
                    if (c == '\t') {
                        autocomplete(command , &len);
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
                    tab_pressed_once = 0;
                }
            }else {
                printf("$ ");
                fgets(command, sizeof(command), stdin);
                command[strcspn(command, "\n")] = '\0';
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
          for (int i=0; commands[i] != NULL; i++) {
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

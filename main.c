#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <termios.h>
#include <dirent.h>
#include <sys/stat.h>

char *commands[] = {"exit","echo","type","pwd","cd","history",NULL};
char *history[1024];
int hist_idx = 0;
int history_nav = -1;
int last_appeend_idx = 0;
const int max_history_limit = 1024;
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

void read_history(int limit){
    int start = 0;
    
    if (limit > 0 && limit < hist_idx) {
        start = hist_idx - limit;
    }
    
    for (int i = start ; i < hist_idx; i++) {
        printf("%5d  %s\n", i + 1, history[i]);
    }
}

void write_history(char *command){
    if (hist_idx < max_history_limit) {
        history[hist_idx++] = strdup(command);
    }
}

void print_history(int idx){
    if(hist_idx == 0) return;
    
    if (idx == -1) {
        printf("%s\n",history[hist_idx - 1]);
    }
}

void get_history(){
    char *hist_file = getenv("HISTFILE");
    if (!hist_file) return;
    
    FILE *fp = fopen(hist_file, "r");
    
    if (fp == NULL) {
        return;
    }else{
       char line[256];
       
       while (fgets(line, sizeof(line), fp)) {
           line[strcspn(line, "\n")] = '\0';
                            
           if (line[0] == '\0') continue;
                            
           write_history(line);
       }
       fclose(fp);
    }
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

void builtin_pwd(){
    char cwd[64];
    getcwd(cwd, sizeof(cwd));
    printf("%s\n",cwd);
}

void builtin_cd(char **path){
    if (strcmp(*path, "~") == 0) {
        chdir(getenv("HOME"));
    }else if (chdir(*path) != 0){
        printf("cd: %s: No such file or directory\n",*path);
    }
}

void builtin_echo(char **string){
    char *args[64];
    parse_args(*string, args);
    
    char *output_file = NULL;
    int redirect_status = handler_redirection(args, &output_file);
    
    if (redirect_status == -1) {
        printf("echo: missing file operand\n");
        return;
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
}

void builtin_type(char **cmd_name){
    int flag = 0;
    
        for (int i = 0; commands[i] != NULL; i++) {
            if (strcmp(*cmd_name, commands[i]) == 0) {
                printf("%s is a shell builtin\n", *cmd_name);
                flag = 1;
                break;
            }
        }
    
        if (!flag) {
            const char *env = getenv("PATH");
            char *path_copy = strdup(env);
            char *dir = strtok(path_copy, ":");
    
            char *cmd = *cmd_name;
    
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
            printf("%s: not found\n", *cmd_name);
        }
}

void builtin_other(char *command) {
    char *args[64];
    parse_args(command, args);

    char *output_file = NULL;
    int redirect_status = handler_redirection(args, &output_file);

    if (redirect_status == -1) {
        printf("missing file operand\n");
        return;
    }

    pid_t pid = fork();

    if (pid == 0) {
        if (redirect_status > 0) {
            int flags = O_WRONLY | O_CREAT;

            if (redirect_status == 1 || redirect_status == 2)
                flags |= O_TRUNC;
            else
                flags |= O_APPEND;

            int fd = open(output_file, flags, 0644);
            if (fd < 0) {
                perror("open");
                exit(1);
            }

            if (redirect_status == 1 || redirect_status == 3)
                dup2(fd, STDOUT_FILENO);
            else
                dup2(fd, STDERR_FILENO);

            close(fd);
        }

        execvp(args[0], args);
        printf("%s: command not found\n", args[0]);
        exit(1);
    } else {
        waitpid(pid, NULL, 0);
    }
}

int is_builtin(char *cmd) {
    return strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "echo") == 0 ||
           strcmp(cmd, "type") == 0 ||
           strcmp(cmd, "exit") == 0;
}

int run_builtin(char **args) {
    if (strcmp(args[0], "pwd") == 0) {
        builtin_pwd();
        return 1;
    }

    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            chdir(getenv("HOME"));
        } else {
            builtin_cd(&args[1]);
        }
        return 1;
    }

    if (strcmp(args[0], "echo") == 0) {
        // your builtin_echo expects string, but pipeline gives args
        // easiest: just print args[1..]
        for (int i = 1; args[i] != NULL; i++) {
            if (i > 1) printf(" ");
            printf("%s", args[i]);
        }
        printf("\n");
        return 1;
    }

    if (strcmp(args[0], "type") == 0) {
        if (args[1] != NULL) {
            builtin_type(&args[1]);
        }
        return 1;
    }

    if (strcmp(args[0], "exit") == 0) {
        exit(0); // only exits the child, not parent
    }

    return 0;
}

char *trim(char *s){
    while (*s == ' ') s++;
    
    if(*s == 0) return s;
   
   char *end = s + strlen(s)-1;
   while (end > s && *end == ' ') end--;
   
   *(end+1) = '\0';
   return s;
}

void handle_pipeline(char *command){
    char *segments[64];
    int seg_count = 0;
    
    char *cmd_copy = strdup(command);
    char *part = strtok(cmd_copy, "|");
    
    while (part != NULL && seg_count < 63) {
        segments[seg_count++] = strdup(trim(part));
        part = strtok(NULL, "|");
    }
    segments[seg_count] = NULL;
    
    free(cmd_copy);
    
    // If only 1 segment, no pipeline needed
    if (seg_count == 1) {
        char *args[64];
        parse_args(segments[0], args);
        execvp(args[0], args);
        perror("execvp");
        exit(1);
    }
        
    int prev_read = -1;
    pid_t pids[64];
        
    for (int i = 0; i < seg_count; i++) {
        int pipefd[2];

        // Create pipe except for last command
        if (i < seg_count - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                return;
            }
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return;
        }

        if (pid == 0) {
            // CHILD

            // If not first command, read from prev pipe
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }

            // If not last command, write into current pipe
            if (i < seg_count - 1) {
                close(pipefd[0]);               // close read end
                dup2(pipefd[1], STDOUT_FILENO); // stdout -> pipe write
                close(pipefd[1]);
            }

            // Parse args and exec
            char *args[64];
            parse_args(segments[i], args);
            
            if (is_builtin(args[0])) {
                run_builtin(args);
                exit(0);
            }


            execvp(args[0], args);
            printf("%s: command not found\n", args[0]);
            exit(1);
        }

        // PARENT
        pids[i] = pid;

        // Parent doesn't need prev_read anymore
        if (prev_read != -1) close(prev_read);

        // Parent keeps read end for next command
        if (i < seg_count - 1) {
            close(pipefd[1]);       // close write end
            prev_read = pipefd[0];  // next command reads from here
        }
    }
        
    // Wait for all children
    for (int i = 0; i < seg_count; i++) {
        waitpid(pids[i], NULL, 0);
    }
        
    // Free segments
    for (int i = 0; i < seg_count; i++) {
        free(segments[i]);
    }
}

int cmp(const void *a, const void *b) {
    char *s1 = *(char **)a;
    char *s2 = *(char **)b;
    return strcmp(s1, s2);
}

int findprefix(const char *str1, const char *str2, char *found) {
    int i = 0;

    while (str1[i] && str2[i] && str1[i] == str2[i]) {
        found[i] = str1[i];
        i++;
    }

    found[i] = '\0';
    return i;
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
    }else {
        qsort(predictions, pred_count, sizeof(char *), cmp);
    
        // find LCP
        char prefix[1024];
        strcpy(prefix, predictions[0]);
    
        for (int i = 1; i < pred_count; i++) {
            char temp[1024];
            findprefix(prefix, predictions[i], temp);
            strcpy(prefix, temp);
    
            if (prefix[0] == '\0') break;
        }
    
        int prefix_len = strlen(prefix);
    
        // CASE 1: LCP is longer than what user typed → complete in-place
        if (prefix_len > *len) {
            write(STDOUT_FILENO, prefix + *len, prefix_len - *len);
    
            strcpy(command, prefix);
            *len = prefix_len;
    
            tab_pressed_once = 0;
            return;
        }
    
        // CASE 2: LCP is same as input → normal double-tab list behavior
        if (tab_pressed_once == 0) {
            write(STDOUT_FILENO, "\a", 1);
            tab_pressed_once = 1;
            return;
        }
    
        tab_pressed_once = 0;
    
        printf("\n");
        for (int i = 0; i < pred_count; i++) {
            printf("%s", predictions[i]);
            if (i != pred_count - 1) printf("  ");
        }
        printf("\n");
    
        printf("$ ");
        write(STDOUT_FILENO, command, *len);
        return;
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

int get_user_input(char *command, int max_len){
    int len = 0;
    
        if (isatty(STDIN_FILENO)) {
            enable_raw_mode();
            printf("$ ");
    
            while (1) {
                char c;
                if (read(STDIN_FILENO, &c, 1) <= 0) {
                    command[0] = '\0';
                    return 0;
                }
                
                if (c == 27) {
                    char seq[2];
                    if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
                    if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
                
                    // UP arrow: ESC [ A
                    if (seq[0] == '[' && seq[1] == 'A') {
                        if (hist_idx > 0) {
                            if (history_nav == -1) history_nav = hist_idx - 1;
                            else if (history_nav > 0) history_nav--;
                
                            while (len > 0) {
                                write(STDOUT_FILENO, "\b \b", 3);
                                len--;
                            }
                
                            strcpy(command, history[history_nav]);
                            len = strlen(command);
                            write(STDOUT_FILENO, command, len);
                        }
                        continue;
                    }
                
                    // DOWN arrow: ESC [ B
                    if (seq[0] == '[' && seq[1] == 'B') {
                        if (history_nav == -1) continue;
                
                        if (history_nav < hist_idx - 1) history_nav++;
                        else history_nav = -1;
                
                        while (len > 0) {
                            write(STDOUT_FILENO, "\b \b", 3);
                            len--;
                        }
                
                        command[0] = '\0';
                
                        if (history_nav != -1) {
                            strcpy(command, history[history_nav]);
                            len = strlen(command);
                            write(STDOUT_FILENO, command, len);
                        }
                        continue;
                    }
                
                    continue;
                }
        
                if (c == '\t') {
                    autocomplete(command, &len);
                    continue;
                }
    
                if (c == '\n' || c == '\r') {
                    command[len] = '\0';
                    printf("\n");
                    break;
                }
    
                if (c == 127 || c == 8) {  // backspace
                    if (len > 0) {
                        len--;
                        command[len] = '\0';
                        write(STDOUT_FILENO, "\b \b", 3);
                    }
                    continue;
                }
    
                if (len < max_len - 1) {
                    command[len++] = c;
                    write(STDOUT_FILENO, &c, 1);
                }
    
                tab_pressed_once = 0;
            }
        } else {
            printf("$ ");
            if (!fgets(command, max_len, stdin)) {
                command[0] = '\0';
                return 0;
            }
            command[strcspn(command, "\n")] = '\0';
            len = strlen(command);
        }
    
        return len;
}

int main(int argc, char *argv[]) {
  get_executables();
  get_history();
  setbuf(stdout, NULL);
  char command[1024];

  while (1) {
      int len = get_user_input(command, sizeof(command));
      disable_raw_mode();
      
      if (command[0] == '\0') continue;
      
      write_history(command);
      
      if (strchr(command, '|') != NULL) {
          handle_pipeline(command);
          continue;
      }
      
      if (strcmp(command, "exit") == 0) {
          
          char *hist_file = getenv("HISTFILE");
          
          if (hist_file != NULL) {
              FILE *fp = fopen(hist_file, "w");
              if (fp != NULL) {
                  for (int i = 0; i < hist_idx; i++) {
                      fprintf(fp, "%s\n", history[i]);
                  }
                  fclose(fp);
              }
          }
          
          break;
      }else if (strcmp(command, "pwd") == 0) {
          builtin_pwd();
      }else if (strncmp(command, "cd ", 3) == 0) {
         char *path = command+3;
         builtin_cd(&path);
      }else if (strncmp(command, "echo ", 5) == 0) {
          char *string = command + 5;
          builtin_echo(&string);
      }else if (strncmp(command, "type ", 5) == 0) {
          char *cmd = command+5;
          builtin_type(&cmd);
      }else if (strcmp(command, "history") == 0 || strncmp(command, "history ", 8) == 0) {
          if (strcmp(command, "history") == 0) {
              read_history(max_history_limit);
          }else if (strncmp(command+8, "-r ", 3) == 0) {
              
              char *history_file = command + 11;
              FILE *fp = fopen(history_file, "r");
              
              if (fp == NULL) {
                  perror("file");
              }else{
                  char line[256];
                  
                  while (fgets(line, sizeof(line), fp)) {
                      line[strcspn(line, "\n")] = '\0';
                      
                      if (line[0] == '\0') continue;
                      
                      write_history(line);
                    }
              }
              fclose(fp);
          }else if (strncmp(command+8, "-w ", 3) == 0) {
              
              char *history_file = command + 11;
              FILE *fp = fopen(history_file, "w");
              
              if (fp == NULL) {
                  perror("file");
              }else{
                for (int i = 0; i < hist_idx; i++) {
                    fprintf(fp, "%s\n", history[i]);
                }
                fclose(fp);
              }
          }else if (strncmp(command+8, "-a ", 3) == 0) {
              
              char *history_file = command + 11;
              FILE *fp = fopen(history_file, "a");
              
              if (fp == NULL) {
                  perror("file");
              }else{
                for (int i = last_appeend_idx; i < hist_idx; i++) {
                    fprintf(fp, "%s\n", history[i]);
                }
                last_appeend_idx = hist_idx;
                fclose(fp);
              }
          }else{
              int limit = atoi(command + 8);
              read_history(limit);
          }
      }else{
          builtin_other(command);
      }
  }
  return 0;
}

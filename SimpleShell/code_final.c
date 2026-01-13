#include <stdio.h>
#include <stdlib.h>
#include <string.h>   
#include <unistd.h>
#include <sys/wait.h> 
#include <sys/time.h> 
#include <signal.h>   
#include <time.h>  

#define MAX_SIZE 1024 // Max Size for input string 
#define MAX_ARG 100   // Max arguments for history entries
#define MAX_HISTORY 100 // MAx history size

// Structre to story info for histories
typedef struct {
    char command[MAX_SIZE];    // stores command string
    pid_t pid;                 // stores the id
    struct timeval start;      // stores start time 
    struct timeval end;        // stores end time 
} HistoryEntry;


HistoryEntry history[MAX_HISTORY];      //initializing array to store commands in history 
int history_count = 0;

void add_to_history(const char* input,pid_t pid,struct timeval start, struct timeval end) {
    if (history_count < MAX_HISTORY) {
        strncpy(history[history_count].command, input, MAX_SIZE); //copying input to history .
        history[history_count].pid = pid;
        history[history_count].start = start;
        history[history_count].end = end;
        history_count++;
    }
}

// Displaying only commands from history when history is typed as input.
void show_history() {
    printf("Showing only history cmds: ");
    for (int i = 0; i < history_count; i++) {
        printf("%d  %s\n", i + 1, history[i].command);
    }
}

//Dispplaying detailed history when process terminated via ctrl+c.
void show_detailed_history() {
    printf("\n -- SHOWING DETAILED HISTORY --\n");
    for (int i = 0; i < history_count; i++) {
        double duration = (history[i].end.tv_sec - history[i].start.tv_sec) +
                          (history[i].end.tv_usec - history[i].start.tv_usec) / 1e6; //Total duration including microsecond differnce.

        printf("Command: %s\n", history[i].command);
        printf("  PID: %d\n", history[i].pid);
        printf("  Start Time : %ld\n", (long)history[i].start.tv_sec); // approx Starting time
         printf("  End Time : %ld\n", (long)history[i].end.tv_sec); //approx end time
        printf("  Duration: %.6f seconds\n\n", duration);
    }
}

// custom handler to show detailed history when program terminates 
void handle_sigint(int sig) {
    if(sig==SIGINT) {
        show_detailed_history();
        exit(0);
    }
}

// Function to read input 
char* read_user_input() {
    static char inp_arr[MAX_SIZE]; // inp_arr to store input
    if (fgets(inp_arr, MAX_SIZE, stdin) == NULL) { 
        inp_arr[0] = '\0'; // if fails set inp_arr empty
    }
    inp_arr[strcspn(inp_arr, "\n")] = '\0'; // remove new line character.
    return inp_arr;
}


// fxn for splitting args of input. 
void parse_input(char* input, char** args) {
    int i = 0;
    char* token = strtok(input, " "); //splitting by spaces
    while (token != NULL && i < MAX_ARG - 1) {
        args[i++] = token;
        token = strtok(NULL, " ");  //getting next token
    }
    args[i] = NULL;    //terminate
}

void execute_single_command(char** args, const char* input_copy) {
    struct timeval start, end;
    gettimeofday(&start, NULL); //Record start time 

    pid_t pid = fork(); 
    if (pid == -1) {  
        printf("Fork failed"); 
    } else if (pid == 0) { //child process
        if (execvp(args[0], args) == -1) { 
            perror("Execution faailed");
        }
        exit(EXIT_FAILURE);
    } else { // parent process 
        int status;
        waitpid(pid, &status, 0);             // wait for child to finish
        gettimeofday(&end, NULL);             // records end time
        add_to_history(input_copy, pid, start, end); // add cmds to history
    }
}

int count_pipes(const char* str) {
    int count = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '|') {
            count++;
        }
    }
    return count;
}


void execute_piped_commands(char* input, const char* input_copy) {
    struct timeval start, end;
    int ct = count_pipes(input);
    int num_cmds = ct + 1;
    gettimeofday(&start, NULL); //record start time
    char* pipe_cmds[num_cmds];        // Array to hold pipe cmds
    int i = 0;           
    char* token = strtok(input, "|"); // Split input at pipes
    while (token != NULL && i < num_cmds) {
        // store commands
        pipe_cmds[i++] = token;
        token = strtok(NULL, "|"); // next token
    }

    int prev_pipe = 0; // input for current command
    int pipefd[2];  // Pipe file descriptor
    pid_t pids[num_cmds]; // store PIDs of all children

    for (int i = 0; i < num_cmds; i++) {
        if(pipe(pipefd)== -1) { // Create a pipe
            perror("pipe");
            exit(EXIT_FAILURE);
        };        
        pid_t pid = fork(); 

        if (pid == -1) {        // check if fork failed 
            printf("Fork failed\n"); 
            exit(1);
        } else if (pid == 0) {      // child process
           
            if (i > 0) {
                dup2(prev_pipe, STDIN_FILENO);
                close(prev_pipe);
            }        
            if (i < num_cmds - 1) {     // If not last command
                dup2(pipefd[1], STDOUT_FILENO);      // stdout goes to  pipe write end
            }

             close(pipefd[0]);       // close unused read end. child does not read from its own pipe .
            close(pipefd[1]);       // close write end in child .          

            char* args[100];                
            parse_input(pipe_cmds[i], args);        // parse commands into args.

            if (execvp(args[0], args) == -1) {  //Execute command
                perror("execution failed");
                exit(EXIT_FAILURE);
            }
        } else {            // parent process
            pids[i]=pid;
            close(pipefd[1]);  //close write end of pipe
            if (prev_pipe != 0) close(prev_pipe);  // close old read end 
            prev_pipe = pipefd[0];  // next cmds reads from this pipe
        }
    }
    if (prev_pipe != 0) close(prev_pipe);

    for (int i = 0; i < num_cmds; i++) {
        waitpid(pids[i], NULL, 0);
    }

    gettimeofday(&end, NULL);              // record end time 
    add_to_history(input_copy, pids[num_cmds-1], start, end);      //add pipe cmd to history
}

void shell_loop() {
    char* input; // pointer to user input
    char input_copy[1024]; // copy for input for history
    char* args[100]; // array for parsed arguments

    do {
        printf("Group_81_A2$ "); 
        fflush(stdout); // flush stdout

        input = read_user_input();
        strncpy(input_copy, input, MAX_SIZE-1); // Copy input for safe running.

        if (strlen(input_copy) == 0) continue; // skip if input is empty

        if (strcmp(input_copy, "history") == 0) { // checks if input is history shows history
            show_history();
            continue;
        }

        if (strcmp(input_copy, "exit") == 0) { // check if input is exit show history and terminate
            show_detailed_history();
            break;
        }

        if (strchr(input_copy, '|')) { // Check for pipe
            execute_piped_commands(input_copy, input_copy);
        } else {
            parse_input(input_copy, args); // if cmd not empty execute command
            if (args[0] != NULL) {
                execute_single_command(args, input_copy);
            }
        }
    } while (1);
}

int main() {
    struct sigaction sig;          
    memset(&sig, 0, sizeof(sig));  // initializze to 0   
    sig.sa_handler = handle_sigint; 
    sigaction(SIGINT, &sig, NULL);  

    shell_loop(); 
    return 0;
}

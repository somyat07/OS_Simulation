#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h> 

#define MAX_SIZE 1024
#define MAX_JOBS 100
#define REPORT_FILE "/tmp/scheduler_report.dat" // USed for reading & writing in the temp file 

typedef struct {
    char name[MAX_SIZE];
    pid_t pid;
    int has_completed;
    int time_sc_executed;
    int time_sc_waited;
} Job;

Job job_list[MAX_JOBS];
int job_count = 0;
int tslice_val = 0;

int pipe_fd[2]; // Pipe for communicating with the scheduler 
pid_t scheduler_pid = -1; // PID of the scheduler process

// Signal handler for child process termination
void handle_sigchld(int sig) {
    int status;
    pid_t pid;
    // Reap any child that has terminated to prevent zombies.
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { 
        // Mark the job as completed in our internal list
        for (int i = 0; i < job_count; i++) {
            if (job_list[i].pid == pid) {
                job_list[i].has_completed = 1;
                break;
            }
        }
    }
}

// Reads the final statistics file written by the scheduler. reading report file
void load_stats_from_report() {
    FILE* report_file = fopen(REPORT_FILE, "r");
    if (!report_file) return;

    int pid, exec_slices, wait_slices;
    // Parse the report file line by line.
    while (fscanf(report_file, "%d,%d,%d\n", &pid, &exec_slices, &wait_slices) == 3) {
        // Find the matching job and update its stats.
        for (int i = 0; i < job_count; i++) {
            if (job_list[i].pid == pid) {
                job_list[i].time_sc_executed = exec_slices;
                job_list[i].time_sc_waited = wait_slices;
                break;
            }
        }
    }
    fclose(report_file);
    remove(REPORT_FILE); //  to clean up the temporary file
}

// Prints the final, formatted report
void print_final_report() {
    printf("\n      Final Job Report       \n\n");
    printf("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-\n");
    printf(" %-20s  %-7s  %-15s  %-15s \n", "Command", "PID", "Completion Time", "Wait Time");
    printf("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-\n");

    for (int i = 0; i < job_count; i++) {
        char completion_time_str[32];
        char wait_time_str[32];
        sprintf(completion_time_str, "%d x %dms", job_list[i].time_sc_executed, tslice_val);
        sprintf(wait_time_str, "%d x %dms", job_list[i].time_sc_waited, tslice_val);

        printf(" %-20s  %-7d  %-15s  %-15s \n",job_list[i].name, job_list[i].pid, completion_time_str, wait_time_str);
    }
   printf("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-\n");
}

// Handles shell shutdown 
void cleanup_and_exit(int sig) {
    if(pipe_fd[1] > 0) {
        close(pipe_fd[1]); // Close pipe to signal scheduler to exit
    }
    
    if (scheduler_pid > 0) {
        kill(scheduler_pid, SIGTERM); // Send termination signal to scheduler 
        waitpid(scheduler_pid, NULL, 0); // Wait for scheduler to finish
    }
    
    load_stats_from_report(); // Read the stats file.
    print_final_report();     // Print the final report
    exit(0);                  // Exit the shell
}

void shell_loop() {
    char input[MAX_SIZE];
    char* args[MAX_SIZE];

    do {
        printf("Group_81$ ");
        fflush(stdout); //immediately runs
        if (fgets(input, sizeof(input), stdin) == NULL) break;
        
        input[strcspn(input, "\n")] = 0; // Remove newline
        if (strlen(input) == 0) continue;

        char input_copy[MAX_SIZE];
        strcpy(input_copy, input);
        int i = 0;
        char* token = strtok(input_copy, " "); // Tokenize input
        while(token != NULL) { args[i++] = token; token = strtok(NULL, " "); }
        args[i] = NULL;
        if (args[0] == NULL) continue;


        if (strcmp(args[0], "exit") == 0) {
            break; //  the loop to shut down
        }
        

        if (strcmp(args[0], "submit") == 0) {
            if (args[1] == NULL) continue;
            if (args[2] != NULL) {                // extra args found
            printf("Error: submit takes exactly one executable \n");
            continue;
            }

            pid_t pid = fork(); // Create a new process
            if (pid == 0) {
                // child process
                raise(SIGSTOP); // Pause - waiting for scheduler. 
                execvp(args[1], &args[1]); // Replace with user's command
                perror("execvp failed");
                exit(1);
            } else if (pid > 0) {
                // parent process - shell
                if (job_count < MAX_JOBS) {
                    strcpy(job_list[job_count].name, args[1]);
                    job_list[job_count].pid = pid;
                    job_count++;
                }
                write(pipe_fd[1], &pid, sizeof(pid_t)); // Send PID to scheduler
            } else {
                perror("fork failed");
            }
        } else {
            printf("Unknown command: %s\n", args[0]); 
        }
    } while (1);

    cleanup_and_exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 3) { 
        fprintf(stderr, "Usage: %s <NCPU> <TSLICE_ms>\n", argv[0]); return 1; }

    tslice_val = atoi(argv[2]);

    if (pipe(pipe_fd) == -1) { 
        perror("pipe creation failed"); return 1; 
    }

    scheduler_pid = fork(); // Create the scheduler process
    if (scheduler_pid == 0) {
        // scheduler procss 
        close(pipe_fd[1]); // Scheduler only reads from the pipe.
        char fd_str[10];
        sprintf(fd_str, "%d", pipe_fd[0]);
        char* scheduler_args[] = {"./SimpleScheduler", argv[1], argv[2], fd_str, NULL};
        execvp(scheduler_args[0], scheduler_args); // Replace with scheduler program
        perror("execvp for scheduler failed");
        exit(1);
    } else if (scheduler_pid > 0) {
        // Shell process
        close(pipe_fd[0]); // Shell only writes to the pipe.
        signal(SIGINT, cleanup_and_exit);  // Handle Ctrl-C
        signal(SIGCHLD, handle_sigchld); // Handle child termination
        shell_loop(); // Start main loop
    } else {
        perror("fork failed");
        return 1;
    }
    return 0;
}
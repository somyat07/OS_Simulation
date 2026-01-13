#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h> 
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>

#define MAX_JOBS 100
#define REPORT_FILE "/tmp/scheduler_report.dat" // USed for reading & writing in the temp file 

// Data structure to hold statistics for each job. // lec05 (Process Descriptor/PCB)
typedef struct {
    pid_t pid;                  // Process ID.
    int time_sc_executed;   // Counter for execution time.
    int time_sc_waited;     // Counter for wait time.
} JobInfo;

JobInfo job_stats[MAX_JOBS]; // Array to store stats for all jobs
int job_stats_ct = 0;     // Total number of jobs tracked

// The ready queue for processes waiting for the CPU. // lec12 (Scheduler)
pid_t ready_queue[MAX_JOBS];
int queue_front = 0, queue_rear = -1, queue_ct = 0;

// Array of currently running processes
pid_t running_processes[MAX_JOBS];
int running_count = 0;

//Variables for scheduler configuration 
int NCPU;         // Number of CPUs available.
int TSLICE;       // Duration of a time slice in ms.
int PIPE_READ_FD; // File descriptor to read PIDs from the shell. // lec08(pipes)
int isActive = 0;
struct itimerval timer; // Timer for generating interrupts. // lec12 {timer interrupt topic}

// Adds a process ID to the end of the ready queue. // lec13 {round robin}
void enqueue(pid_t pid) {
    if (queue_ct < MAX_JOBS) {
        if (queue_rear == MAX_JOBS - 1) queue_rear = -1;
        ready_queue[++queue_rear] = pid;
        queue_ct++;
    }
}

// Removes a process ID from the front of the ready queue. 
pid_t dequeue() {
    if (queue_ct > 0) {
        pid_t pid = ready_queue[queue_front++];
        if (queue_front == MAX_JOBS) queue_front = 0;
        queue_ct--;
        return pid;
    }
    return -1;
}

// Finds the index of a job in the job_stats array by its PID. 
int find_job_idx(pid_t pid) {
    for (int i = 0; i < job_stats_ct; i++) {
        if (job_stats[i].pid == pid) return i;
    }
    return -1;
}

// Adds a new job to the statistics tracker. 
void add_job(pid_t pid) {
    if (job_stats_ct < MAX_JOBS && find_job_idx(pid) == -1) {
        job_stats[job_stats_ct].pid = pid;
        job_stats[job_stats_ct].time_sc_executed = 0;
        job_stats[job_stats_ct].time_sc_waited = 0;
        job_stats_ct++;
    }
}

// Checks if a process with a given PID is still alive. // lec05 process
int is_process_alive(pid_t pid) {
    return (kill(pid, 0) == 0); // lec08(Signals)
}

// scheduling function called by the timer interrupt
void schedule(int sig) {
    pid_t previously_running[MAX_JOBS];
    int prev_running_count = running_count;
    memcpy(previously_running, running_processes, sizeof(pid_t) * running_count); 

    // PHASE 1: SCHEDULE THE *NEXT* TIME SLICE
    
    // Stop currently running jobs and move them back to the ready queue.
    for (int i = 0; i < prev_running_count; i++) {
        pid_t pid = previously_running[i];
        if (is_process_alive(pid)) {
            kill(pid, SIGSTOP); // Pause the process. 
            enqueue(pid);       // Add it back to the wait queue.
        }
    }
    running_count = 0;

    // Fill CPU slots from the ready queue.
    while (running_count < NCPU && queue_ct > 0) { // implementing scheduling policies - lec13
        pid_t pid_to_run = dequeue(); // Get the next process.
        if (pid_to_run != -1 && is_process_alive(pid_to_run)) {
            running_processes[running_count++] = pid_to_run; // Add to running list.
            kill(pid_to_run, SIGCONT); // Resume the process. 
        }
    }
    // PHASE 2: ACCOUNT FOR THE *PREVIOUS* TIME SLICE
    // Increment execution time for processes that just ran.
    for (int i = 0; i < prev_running_count; i++) {
        int stat_idx = find_job_idx(previously_running[i]);
        if (stat_idx != -1) {
            job_stats[stat_idx].time_sc_executed++; // used this logic from reference - book-rev11 (Scheduling)
        }
    }
    // Increment wait time for all processes still in the ready queue.
    int current_q_idx = queue_front;
    for (int i = 0; i < queue_ct; i++) {
        pid_t waiting_pid = ready_queue[current_q_idx];
        int stat_idx = find_job_idx(waiting_pid);
        if (stat_idx != -1) {
             job_stats[stat_idx].time_sc_waited++; //used this logic from reference - book-rev11 (Scheduling)
        }
        current_q_idx = (current_q_idx + 1) % MAX_JOBS; 
    }
}
// Handles a new process submitted by the shell.
void schedule_new_process(pid_t new_pid) {
    add_job(new_pid); // Add to tracking.
    enqueue(new_pid);      // Add to ready queue.

    // If this is the first process, start the scheduling timer.
    if (!isActive) {
        setitimer(ITIMER_REAL, &timer, NULL); // Start the periodic timer. lec-12
        isActive = 1;
        schedule(0); // Immediately schedule to run the first job.
    }
}
// Cleans up and writes the final report upon termination.
void handle_termination(int sig) {
    // Open the report file for writing the final status  
    FILE* report_file = fopen(REPORT_FILE, "w");
    if (report_file) {
        for (int i = 0; i < job_stats_ct; i++) {
            fprintf(report_file, "%d,%d,%d\n",
                    job_stats[i].pid,
                    job_stats[i].time_sc_executed,
                    job_stats[i].time_sc_waited);
        }
        fclose(report_file);
    }
    
    // Terminate any remaining running processes.
    for (int i = 0; i < running_count; i++) {
        if (is_process_alive(running_processes[i])) {
            kill(running_processes[i], SIGKILL); // Forcefully terminate. 
        }
    }
    // Terminate any remaining processes in the ready queue.
    while(queue_ct > 0) {
        pid_t pid = dequeue();
        if (is_process_alive(pid)) {
            kill(pid, SIGKILL);
        }
    }

    close(PIPE_READ_FD); // Close the pipe from the shell
    exit(0); // Exit the scheduler process
}


// Main entry point for the scheduler program.
int main(int argc, char *argv[]) {
    if (argc != 4) {
        return 1; 
    }

    // Parse command-line arguments.
    NCPU = atoi(argv[1]); //convert to integer (atoi)
    TSLICE = atoi(argv[2]);
    PIPE_READ_FD = atoi(argv[3]);

    // Register signal handlers. 
    signal(SIGTERM, handle_termination); // For shutdown.
    signal(SIGALRM, schedule);           // For the scheduling timer tick.

    // Set up the timer for preemptive multitasking. lec-12 
    timer.it_value.tv_sec = TSLICE / 1000;
    timer.it_value.tv_usec = (TSLICE % 1000) * 1000;
    timer.it_interval = timer.it_value; // Make it a repeating timer.

    // read new process PIDs from the shell via a pipe.
    pid_t new_pid;
    while (read(PIPE_READ_FD, &new_pid, sizeof(pid_t)) > 0) { 
        schedule_new_process(new_pid);
    }
    // If the pipe closes, the shell has exited. Terminate.
    handle_termination(0);
    return 0;
}
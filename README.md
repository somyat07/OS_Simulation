# Operating Systems Projects

A collection of three foundational Operating Systems projects implementing core concepts including shell programming, process scheduling, and demand paging. These projects demonstrate practical implementations of concepts like process creation, inter-process communication, signal handling, and memory management.

## Team

**Group 81**
- **Somya** (2024560)
- **Arjun Khatri** (2024106)

---

## Projects Overview

### 1. SimpleShell - Unix Command Line Shell
A custom Unix shell implementation supporting command execution, pipelines, and execution history.

### 2. SimpleScheduler - Preemptive Process Scheduler
A round-robin process scheduler with preemptive multitasking capabilities.

### 3. SimpleSmartLoader - Demand Paging ELF Loader
An intelligent ELF loader implementing lazy loading through demand paging.

---

## Project 1: SimpleShell

### Features
- Interactive command-line interface with custom prompt
- Command parsing and execution using `fork()` and `execvp()`
- Pipeline support for chaining commands with `|`
- In-memory command history tracking
- Signal handling for graceful termination (Ctrl-C)
- Execution statistics (PID, start time, duration)

### Supported Commands
All standard Unix commands including:
- File operations: `ls`, `ls -l`, `ls -R`, `cat`, `sort`, `uniq`
- Text processing: `grep`, `wc`, `echo`
- Local executables: `./fib`, `./helloworld`
- Pipelines: `cat file.c | wc -l`, `cat file.c | grep print | wc -l`

### Usage
```bash
# Compile
make shell

# Run
./SimpleShell

# Example commands
Group_81_A2$ ls -l
Group_81_A2$ cat fib.c | wc -l
Group_81_A2$ history
Group_81_A2$ exit
```

### Technical Implementation
- **Command Reading**: Safe input handling with `fgets()`
- **Parsing**: Tokenization using `strtok()` for whitespace and `|` detection
- **Single Commands**: Process creation with `fork()` and execution with `execvp()`
- **Pipelines**: Multi-process coordination using `pipe()` and `dup2()`
- **History**: In-memory tracking with timestamps using `gettimeofday()`
- **Signal Handling**: `sigaction()` for SIGINT (Ctrl-C) interception

### Limitations
- No I/O redirection (`>`, `<`, `>>`)
- No background execution (`&`)
- No shell built-ins (`cd`, `export`, `alias`)
- Fixed buffer sizes (max 100 arguments)
- No persistent history across sessions
- Simple quote handling

### Key Contributions
- **Somya**: Shell loop, parsing logic, single command execution, history management, signal handling integration
- **Arjun Khatri**: Pipeline execution using fork/pipe/dup2, debugging and testing

---

## Project 2: SimpleScheduler

### Features
- Round-robin process scheduling algorithm
- Preemptive multitasking with configurable time slices
- Multi-core CPU simulation (configurable NCPU)
- Process statistics tracking (execution time, wait time)
- Inter-process communication via pipes
- Comprehensive job completion reports

### Architecture
The system consists of two programs:

**SimpleShell**
- User interface for job submission
- Process creation and suspension
- Signal handling for job completion (SIGCHLD)
- Report generation and display

**SimpleScheduler**
- Background daemon for CPU scheduling
- Ready queue management
- Timer-based preemption (SIGALRM)
- Process control (SIGSTOP, SIGCONT)
- Statistics collection and reporting

### Usage
```bash
# Compile
make scheduler

# Run (NCPU = number of cores, TSLICE = time slice in ms)
./SimpleShell 2 100

# Submit jobs
SimpleShell$ submit ./a.out
SimpleShell$ submit ./b.out
SimpleShell$ exit

# Output displays:
# Command | PID | Completion Time | Wait Time
```

### Technical Implementation
- **Process Control**: `fork()`, `execvp()`, `raise(SIGSTOP)` for suspended creation
- **Scheduling**: Timer interrupts with `setitimer()` and `SIGALRM`
- **Preemption**: `kill(pid, SIGSTOP)` and `kill(pid, SIGCONT)` for context switching
- **IPC**: Unnamed pipes for shell-scheduler communication
- **Process Tracking**: `waitpid(-1, &status, WNOHANG)` for zombie prevention
- **Reporting**: Temporary file (`/tmp/scheduler_report.dat`) for statistics

### Workflow
1. Shell creates pipe and forks scheduler process
2. User submits jobs via `submit` command
3. Shell creates and suspends jobs, sends PIDs to scheduler
4. Scheduler maintains ready queue and timer
5. Every TSLICE ms: pause running jobs, resume next jobs from queue
6. Track execution and wait statistics per process
7. On exit: scheduler writes report, shell displays results

### Key Contributions
- **Arjun Khatri**: Scheduler implementation, ready queue, timer setup, signal-based scheduling, statistics management, report generation
- **Somya**: Shell implementation, user input handling, process creation, SIGCHLD handling, pipe communication, report display

---

## Project 3: SimpleSmartLoader

### Features
- Demand paging implementation for ELF executables
- Lazy loading strategy (load only when needed)
- Page fault handling through SIGSEGV interception
- Support for code, data, and BSS segments
- Internal fragmentation calculation
- Memory usage and page fault statistics

### Concept
Instead of loading an entire program into memory at startup (eager loading), this loader:
1. Parses ELF headers but maps nothing initially
2. Jumps to the program's entry point (unmapped address)
3. Catches the resulting SIGSEGV as a "page fault"
4. Maps and loads only the requested 4KB page
5. Resumes execution
6. Repeats for each new page accessed

### Usage
```bash
# Compile
make loader

# Run
./SimpleSmartLoader ./test_program

# Output shows:
# - Number of page faults
# - Number of page allocations
# - Internal fragmentation (KB)
```

### Technical Implementation

**Signal Handler Setup**
```c
sa.sa_flags = SA_SIGINFO;
sigaction(SIGSEGV, &sa, NULL);
```

**Page Fault Handler Logic**
1. **Identify Segment**: Find which PT_LOAD segment contains fault address
2. **Page Alignment**: Calculate 4KB boundary using `fault_addr & ~(PAGE_SIZE - 1)`
3. **Map Page**: Use `mmap()` with `MAP_FIXED` for zero-filled anonymous page
4. **Load Data**: Handle three cases:
   - **Pure BSS**: Page beyond file data (already zeroed by mmap)
   - **Pure File**: Page entirely within file data (lseek + read 4KB)
   - **Straddle**: Page partially file, partially BSS (lseek + read partial)

**Fragmentation Calculation**
```c
uintptr_t page_start_addr = (uintptr_t)fault_addr & ~(PAGE_SIZE - 1);
uintptr_t page_offset = page_start_addr - phdr->p_vaddr;
off_t file_offset = phdr->p_offset + page_offset;
```

### Advantages over Assignment 1
- **Faster Startup**: No upfront loading delay
- **Lower Memory Usage**: Only accessed pages consume memory
- **Realistic OS Behavior**: Mimics how real operating systems load programs

### Key Contributions
- **Somya**: 3-case loading logic (BSS/file/straddle), fragmentation algorithm, Makefile, test executables, integration testing
- **Arjun Khatri**: ELF header parsing, signal handler registration, mmap page mapping, fault/allocation counters

---

## Building and Running

### Prerequisites
- GCC compiler
- Linux/Unix environment
- Standard C library and POSIX headers

### Compilation
```bash
# Build all projects
make all

# Build individual projects
make shell
make scheduler
make loader

# Clean build artifacts
make clean
```

### Testing
Each project includes test files:
- **SimpleShell**: `fib.c`, `helloworld.c`, `file.txt`
- **SimpleScheduler**: Sample executables for job submission
- **SimpleSmartLoader**: `fib.c`, `sum.c` (compiled with nostdlib)

---

## Technical Concepts Demonstrated

### System Calls
- `fork()` - Process creation
- `execvp()` - Program execution
- `pipe()` - Inter-process communication
- `dup2()` - File descriptor duplication
- `mmap()` - Memory mapping
- `kill()` - Signal sending
- `waitpid()` - Process synchronization

### Signal Handling
- `SIGINT` - Interrupt signal (Ctrl-C)
- `SIGCHLD` - Child process termination
- `SIGALRM` - Timer interrupts
- `SIGSEGV` - Segmentation fault / page fault
- `SIGSTOP` / `SIGCONT` - Process suspension/resumption

### Memory Management
- Virtual memory addressing
- Page alignment and boundaries
- ELF file format parsing
- BSS segment handling
- Internal fragmentation

### Process Scheduling
- Round-robin algorithm
- Preemptive multitasking
- Time slice management
- Ready queue implementation

---

## References

Based on lecture materials covering:
- **Lectures 6-8**: Process management, fork/exec, pipes, signals
- **Lectures 6-13**: Advanced process control and scheduling
- **Lectures 15-20**: Memory management and ELF loaders

---

## License

These projects were developed as part of an Operating Systems course curriculum.

---

## Acknowledgments

Special thanks to our instructors for guidance on OS concepts and implementation strategies.

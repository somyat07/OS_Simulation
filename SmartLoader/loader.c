#include "loader.h"
#include <signal.h>   // For sigaction (signal handling)
#include <stdint.h>   // For uintptr_t (pointer arithmetic)
#include <sys/ucontext.h> // For ucontext_t (in signal handler)

// Page size is 4KB
#define PAGE_SIZE 4096

// --- Global Variables ---
// needed by the signal handler during program execution.
//  file descriptor for the ELF executable
int fd = -1;
// Global pointer to the program header table
static Elf32_Phdr* global_phdr_table = NULL;
// Global count of program headers
static int global_phnum = 0;

// --- Report Counters ---
static int total_page_faults = 0;
static int total_page_allocations = 0;
static size_t total_internal_fragmentation = 0;


// Reads the main ELF header.
int read_elf_header(int fd, Elf32_Ehdr* ehdr) {
  if (read(fd, ehdr, sizeof(Elf32_Ehdr)) != sizeof(Elf32_Ehdr)) {
    perror("Couldn't read ELF hdr");
    return -1;
  }
  return 0;
}
// Reads the program header table from the ELF file. 
Elf32_Phdr* read_program_headers(int fd, const Elf32_Ehdr* ehdr) {
  size_t table_size = ehdr->e_phnum * ehdr->e_phentsize;
  Elf32_Phdr* phdr_table = malloc(table_size);
  if (!phdr_table) {
    perror("Failed to allocate memory for program headers");
    return NULL;
  }

  if (lseek(fd, ehdr->e_phoff, SEEK_SET) == (off_t)-1) {
    perror("Failure in seeking to phdrs");
    free(phdr_table);
    return NULL;
  }

  if (read(fd, phdr_table, table_size) != table_size) {
    perror("Failure in reading phdrs");
    free(phdr_table);
    return NULL;
  }
  return phdr_table;
}

 //This is the Page Fault Handler.
 //It catches SIGSEGV, finds the responsible segment,
 // maps one 4KB page, and loads the data for that page.

static void page_fault_handler(int sig, siginfo_t *si, void *ucontext) {
    //A fault occurred. Increment the counter.
    total_page_faults++;

    // The address that caused the fault
    void* fault_addr = si->si_addr;
    Elf32_Phdr* phdr = NULL;
    int segment_index = -1;

    //  Find which segment this fault belongs to
    for (int i = 0; i < global_phnum; i++) {
        phdr = &global_phdr_table[i];
        
        if (phdr->p_type == PT_LOAD &&
            fault_addr >= (void*)phdr->p_vaddr && 
            fault_addr < (void*)(phdr->p_vaddr + phdr->p_memsz)) {
            
            segment_index = i;
            break; // Found it
        }
        phdr = NULL;
    }

    // If no segment was found, this is a real error (e.g., NULL ptr)
    if (phdr == NULL) {
        fprintf(stderr, "Unhandled Segmentation Fault at %p\n", fault_addr);
        exit(EXIT_FAILURE);
    }

    // Calculate the start of the 4KB page that faulted
    // This aligns the faulting address down to the nearest 4KB boundary
    uintptr_t page_start_addr = (uintptr_t)fault_addr & ~(PAGE_SIZE - 1);

    // Map only this one page at its required virtual address
    void* mapped_page = mmap((void*)page_start_addr, PAGE_SIZE,
                             PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                             -1, 0);

    if (mapped_page == MAP_FAILED) {
        perror("FAILURE TO MAP MEMORY in handler");
        exit(EXIT_FAILURE);
    }
    total_page_allocations++;

     // Calculate Internal Fragmentation 
    // This logic calculates the wasted space only on the last page.
    uintptr_t segment_end = phdr->p_vaddr + phdr->p_memsz;
    uintptr_t page_end = page_start_addr + PAGE_SIZE;

    // if this page is the LAST page for the segment
    // (i.e., the page ends after the segment, but starts before it ends)
    if (page_end > segment_end && page_start_addr < segment_end) {
        
        // The fragmentation is the unused space on this specific page
        total_internal_fragmentation += (page_end - segment_end);
    }

    // Load data from the ELF file into the new page
  
    // Find the end address of the data that's in the file
    uintptr_t file_data_end = phdr->p_vaddr + phdr->p_filesz;
    // Find the end address of the page we just mapped
    uintptr_t page_end_addr = page_start_addr + PAGE_SIZE;

    // Case 1: Is this page entirely in the .bss section?
    if (page_start_addr >= file_data_end) {
        // Yes. The page should be all zeros.
        // MAP_ANONYMOUS (used in mmap) already zeroed it for us.
    }
    // Case 2: Is this page entirely within the file-backed section?
    else if (page_end_addr <= file_data_end) {
        // Yes. This page is full of .text or .data. Read a full 4KB.
        uintptr_t page_offset = page_start_addr - phdr->p_vaddr;
        off_t file_offset = phdr->p_offset + page_offset;

        if (lseek(fd, file_offset, SEEK_SET) == -1) {
            perror("Failure in seeking (Case 2)"); exit(EXIT_FAILURE);
        }
        if (read(fd, mapped_page, PAGE_SIZE) != PAGE_SIZE) {
            perror("Failure in reading (Case 2)"); exit(EXIT_FAILURE);
        }
    }
    // Case 3: Does this page on both side the boundary (part data, part .bss)?
    else {
        // Yes. Read only the file-backed part.
        size_t bytes_to_read = file_data_end - page_start_addr;

        uintptr_t page_offset = page_start_addr - phdr->p_vaddr;
        off_t file_offset = phdr->p_offset + page_offset;

        if (lseek(fd, file_offset, SEEK_SET) == -1) {
            perror("Failure in seeking (Case 3)"); exit(EXIT_FAILURE);
        }
        if (read(fd, mapped_page, bytes_to_read) != bytes_to_read) {
            perror("Failure in reading (Case 3)"); exit(EXIT_FAILURE);
        }
        // The rest of the page is already zero-filled by MAP_ANONYMOUS.
    }

    //  Return from the handler.
    // The OS will re-run the failed instruction, which now succeeds.
}

// Helper function to register our page_fault_handler for SIGSEGV
void setup_signal_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    // Use SA_SIGINFO to get the faulting address (si_addr)
    sa.sa_flags = SA_SIGINFO; 
    sa.sa_sigaction = page_fault_handler;
    
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("Failed to register SIGSEGV handler");
        exit(EXIT_FAILURE);
    }
}

// Executes the program by jumping to its entry point. 
void execute_entry_point(int entry_addr) {
  // This cast jumps to the virtual address, triggering the first page fault
  int (*_start)() = (int (*)())entry_addr;
  int result = _start();
  printf("User _start return value = %d\n", result);
}

// Cleans up the global file descriptor. 
void loader_cleanup() {
  if (fd != -1) {
    close(fd); 
    fd = -1;
  }
}

//Main loader function.
 // This is modified to only read headers and set up the handler.
 // It does *not* load any segments upfront.
void load_and_run_elf(char** argv) {
  // 1. Open file and read ELF header
  fd = open(argv[1], O_RDONLY); 
  if (fd < 0) {
    perror("Failure in opening file");
    return;
  }

  Elf32_Ehdr ehdr; 
  if (read_elf_header(fd, &ehdr) != 0) {
    loader_cleanup();
    return;
  }

  // 2. Read program headers into global variables
  global_phdr_table = read_program_headers(fd, &ehdr);
  if (!global_phdr_table) {
    loader_cleanup();
    return;
  }
  global_phnum = ehdr.e_phnum;


  // 4. Register our page fault handler
  setup_signal_handler();

  // 5. Get the virtual entry point address from the header
  int entry_point = ehdr.e_entry;

  // We do not close fd or free global_phdr_table yet.
  // The signal handler needs them.

  // 6. Jump to the entry point. This will cause the first page fault.
  if (entry_point != 0) { 
    printf("... jumping to virtual entry point 0x%x\n", entry_point);
    execute_entry_point(entry_point);
  }

  // 7. Print the report
  printf("\n--- SmartLoader Report ---\n");
  printf("Total Page Faults:            %d\n", total_page_faults);
  printf("Total Page Allocations:       %d\n", total_page_allocations);
  printf("Total Internal Fragmentation: %zu bytes (%.2f KB)\n", total_internal_fragmentation, 
         (double)total_internal_fragmentation / 1024.0);

  // 8. Free global resources

  free(global_phdr_table);
  global_phdr_table = NULL;
}

// Main function
int main(int argc, char** argv) {
  if (argc != 2) {
    printf("Usage: %s <ELF Executable>\n", argv[0]);
    exit(1);
  }
  if (access(argv[1], F_OK) != 0) { 
        perror("Error: File does not exist");
        exit(1);
    }
  if (access(argv[1], R_OK) != 0) { 
        perror("file is not readable.");
        exit(1);
    }
    
  load_and_run_elf(argv);
  loader_cleanup(); // Cleans up the global 'fd'
  
  return 0;
}
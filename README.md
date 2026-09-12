# POSIX-compliant Operating System Shell

**Author:** Aleksander Wieczorek

A custom, low-level shell implementation developed in C. This project demonstrates practical knowledge of UNIX/Linux system programming, including process management, inter-process communication (IPC), file descriptor manipulation, and asynchronous signal handling.

## Features

**Core Execution & Batch Processing**
*   Parses and executes external programs in child processes while searching for executables using the `PATH` environment variable.
*   Supports both interactive terminal sessions and non-interactive script execution from files.
*   Intelligently manages the command prompt, displaying it only when standard input is connected to a special character device (TTY).
*   Provides robust error handling for missing files, lack of execution permissions, and execution failures.

**Built-in Commands**
*   Implements native shell commands to optimize performance without spawning new processes:
    *   `exit` – safely terminates the shell process.
    *   `lcd` – changes the current working directory, defaulting to the `HOME` variable if no path is provided.
    *   `lkill` – sends specific signals (defaulting to `SIGTERM`) to processes or process groups based on PID.
    *   `lls` – lists non-hidden files in the current directory using system directory streams.

**Pipelines and I/O Redirection**
*   Supports standard input and output redirection using `<`, `>`, and `>>` operators.
*   Manages file creation, content truncation, and append modes with correct POSIX file permissions.
*   Allows chaining multiple processes into pipelines using the `|` operator, connecting the standard output of one process to the standard input of the next.
*   Supports sequential execution of multiple pipelines separated by `;`.

**Background Processing & Signal Handling**
*   Executes commands asynchronously in the background when terminated with the `&` operator.
*   Actively tracks and reaps terminated background processes to prevent memory leaks (zombie processes).
*   Reports the exact termination status of background tasks, indicating whether they exited normally (with an exit code) or were killed by a specific signal.
*   Implements advanced signal management to ensure that `SIGINT` (CTRL-C) only interrupts the foreground process group, leaving background tasks unaffected.

## Technical Stack & Syscalls Used

This project heavily relies on the POSIX API. Key system calls and library functions utilized include:

*   **Process Management:** `fork`, `execvp`, `wait`, `waitpid`, `setsid`.
*   **File System & I/O:** `read`, `open`, `close`, `chdir`, `opendir`/`fdopendir`, `readdir`, `closedir`.
*   **IPC & Descriptors:** `pipe`, `dup`, `dup2`, `fcntl`.
*   **Signals:** `kill`, `sigaction`, `sigprocmask`, `sigsuspend`.

## Build and Run

```bash
# Compilation
make

# Running the shell
./mshell

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>

#include "builtins.h"
#include "config.h"
#include "siparse.h"
#include "utils.h"
// file mode for created files
#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define SAVED_SIZE 64
int TERMINAL_CHECKER;

////////////// I/O UTILS ///////////////
void write_prompt(int checker)
{
    if (checker == 1)
    {
        printf(PROMPT_STR);
        fflush(stdout);
    }
}

void handle_buffer_overflow(char *buff, int *buff_len)
{
    ssize_t bytes_read;
    // check if there is \n in the buffer
    char *nl = strchr(buff, '\n');
    // it shouldn't happen, left for safety
    if (nl != NULL)
    {
        // there is \n - keep everything after \n
        int after_newline = *buff_len - (nl - buff) - 1;
        if (after_newline > 0)
        {
            memmove(buff, nl + 1, after_newline);
        }
        *buff_len = after_newline;
        buff[*buff_len] = '\0';
    }
    else
    {
        // there is no \n - discard buffer and read until \n, then save what's after
        *buff_len = 0;
        while ((bytes_read = read(0, buff, MAX_LINE_LENGTH - 1)) > 0)
        {
            char *nl = memchr(buff, '\n', bytes_read);
            if (nl != NULL)
            {
                // Found \n - save everything after it
                *buff_len = bytes_read - (nl - buff) - 1;
                if (*buff_len > 0)
                {
                    memmove(buff, nl + 1, *buff_len);
                }
                buff[*buff_len] = '\0';
                break;
            }
            // No \n found - continue reading (discard this chunk)
        }
        // If loop ended without finding \n, buff_len is already 0
    }
}

/////////////////// execution utils ///////////////////

int count_commands(pipeline *pipe)
{
    int pipe_size = 1;
    commandseq *commands = pipe->commands;
    commandseq *start = pipe->commands;
    while (commands->next != start)
    {
        pipe_size++;
        commands = commands->next;
    }
    return pipe_size;
}

void handle_execution_error(const char *name)
{
    // checking works for both: redir and exec
    switch (errno)
    {
    case ENOENT:
        fprintf(stderr, "%s: no such file or directory\n", name);
        break;
    case EACCES:
        fprintf(stderr, "%s: permission denied\n", name);
        break;
    default:
        fprintf(stderr, "%s: exec error\n", name);
        break;
    }
    // exit failure just to be sure
    exit(EXEC_FAILURE);
}

int (*find_builtin(const char *name))(char *[])
{
    for (int i = 0; builtins_table[i].name != NULL; i++)
    {
        if (strcmp(builtins_table[i].name, name) == 0)
            return builtins_table[i].fun;
    }
    return NULL;
}

void execute_single_command(command *cmd, int in_fd, int out_fd)
{
    if (cmd == NULL || cmd->args == NULL)
    {
        return;
    }
    if (in_fd != STDIN_FILENO)
    {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }
    if (out_fd != STDOUT_FILENO)
    {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    // count args
    int arg_count = 1;
    argseq *start = cmd->args;
    argseq *arg = cmd->args;

    // argseq is cyclic list
    while (arg->next != start)
    {
        arg_count++;
        arg = arg->next;
    }
    //  printf("arg_count=%d\n", arg_count);
    // Allocate argv
    char **argv = alloca((arg_count + 1) * sizeof(char *));
    if (!argv)
    {
        exit(EXEC_FAILURE);
    }
    // fill argv
    // watch out for cyclic list
    arg = start;
    for (int i = 0; i < arg_count; i++)
    {
        argv[i] = arg->arg;
        arg = arg->next;
    }
    // NULL at the end
    argv[arg_count] = NULL;

    // check for builtin
    int (*builtin_func)(char *[]) = find_builtin(argv[0]);
    if (builtin_func != NULL)
    {
        // it's a builtin
        int result = builtin_func(argv);
        if (result == BUILTIN_ERROR)
        {
            fprintf(stderr, "Builtin %s error.\n", argv[0]);
        }
        return;
    }

    // not a builtin - execute external command
    if (cmd->redirs != NULL)
    {
        redirseq *redirect = cmd->redirs;
        redirseq *redir_start = cmd->redirs;
        do
        {
            redir *cur = redirect->r;
            int target = -1;
            int flags = -1;

            if (IS_RIN(cur->flags))
            {
                target = STDIN_FILENO;
                flags = O_RDONLY;
            }
            else if (IS_ROUT(cur->flags))
            {
                target = STDOUT_FILENO;
                flags = O_WRONLY | O_CREAT | O_TRUNC;
            }
            else if (IS_RAPPEND(cur->flags))
            {
                target = STDOUT_FILENO;
                flags = O_WRONLY | O_CREAT | O_APPEND;
            }

            if (flags != -1)
            { 
                int fd = open(cur->filename, flags, FILE_MODE);
                if (fd < 0)
                {
                    handle_execution_error(cur->filename);
                }
                dup2(fd, target);
                close(fd);
            }

            redirect = redirect->next;
        } while (redirect != redir_start);
    }

    // child process
    execvp(argv[0], argv);
    handle_execution_error(argv[0]);
}

/////////////////// SIGNALS ////////////
void make_note(int ch, int st);
static sigset_t sigchld_mask;
volatile int fg_count = 0;
volatile int *fg_pids;
int saved[SAVED_SIZE][2];
int last_saved;
void init_sigchld_blocking()
{
    sigemptyset(&sigchld_mask);
    sigaddset(&sigchld_mask, SIGCHLD);
}
void block_sigchld()
{
    sigprocmask(SIG_BLOCK, &sigchld_mask, NULL);
}

void unblock_sigchld()
{
    sigprocmask(SIG_UNBLOCK, &sigchld_mask, NULL);
}

void sigchld_handler(int sig_nb)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        int found = 0;
        for (int i = 0; i < fg_count; i++)
        {
            if (fg_pids[i] == pid)
            {
                fg_pids[i] = fg_pids[fg_count - 1];
                fg_count--;
                found = 1;
                break;
            }
        }
        if (!found && TERMINAL_CHECKER == 1)
        {
            make_note(pid, status);
        }
    }
}

int execute_pipeline(pipeline *current_pipe)
{
    int pipe_len = count_commands(current_pipe);
    int fg = !(current_pipe->flags == INBACKGROUND);
    if (current_pipe->commands->com != NULL && find_builtin(current_pipe->commands->com->args->arg) != NULL)
    {
        execute_single_command(current_pipe->commands->com, STDIN_FILENO, STDOUT_FILENO);
        return 0;
    }
    block_sigchld();
    if (fg)
    {
        fg_pids = alloca(pipe_len * sizeof(int));
        fg_count = 0;
    }
    commandseq *commands = current_pipe->commands;
    if (pipe_len > 0)
    {
        commandseq *start = current_pipe->commands;
        while (commands->next != start)
        {
            if (pipe_len > 1 && commands->com == NULL)
            {
                fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
                return EXEC_FAILURE;
            }
            commands = commands->next;
        }
        commands = current_pipe->commands;
    }

    int pipes[2][2];
    int prev_pipe = -1;
    // iterate through commands
    for (int i = 0; i < pipe_len; i++)
    {
        // cannot finnish yet
        // must use next pipe
        if (i < pipe_len - 1)
        {
            if (pipe(pipes[i % 2]) == -1)
                return EXEC_FAILURE;
        }
        int pid = fork();
        if (pid == 0)
        {
            if (!fg)
            {
                setsid();
            }
            signal(SIGINT, SIG_DFL);
            // if I am not first - take input from prev pipe
            if (i > 0)
            {
                // read the end of prev pipe
                dup2(pipes[prev_pipe][0], STDIN_FILENO);
                close(pipes[prev_pipe][0]);
                close(pipes[prev_pipe][1]);
            }
            // if I am not last - send my output to next input
            if (i < pipe_len - 1)
            {
                dup2(pipes[i % 2][1], STDOUT_FILENO);
                close(pipes[i % 2][0]);
                close(pipes[i % 2][1]);
            }
            execute_single_command(commands->com, STDIN_FILENO, STDOUT_FILENO);
            exit(1);
        }
        else if (pid < 0)
        {
            exit(1);
        }
        else
        {
            if (fg)
            {
                fg_pids[i] = pid;
                fg_count++;
            }
        }
        // close for sure
        if (prev_pipe != -1)
        {
            close(pipes[(i + 1) % 2][0]);
            close(pipes[(i + 1) % 2][1]);
        }
        prev_pipe = i % 2;
        commands = commands->next;
    }
    if (fg)
    {
        sigset_t sigchld_read_mask;
        sigemptyset(&sigchld_read_mask);
        block_sigchld();
        while (fg_count > 0)
        {
            sigsuspend(&sigchld_read_mask);
        }
    }
    unblock_sigchld();
    return 0;
}
//////////////// SIGNALS I/O ////////////////////
void make_note(int ch, int st)
{
    // forget saved if overflow will occur
    if (last_saved == SAVED_SIZE)
        last_saved = 0;
    if (ch > 0)
    {
        saved[last_saved][0] = ch;
        saved[last_saved][1] = st;
        last_saved++;
    }
}

void restore_finished()
{
    // print better prompt
    for (int i = 0; i < last_saved; i++)
    {
        printf("Background process %d terminated. (exited with status %d)\n", saved[i][0], saved[i][1]);
    }
    last_saved = 0;
}
int main(int argc, char *argv[])
{
    TERMINAL_CHECKER = isatty(STDIN_FILENO);
    pipelineseq *ln;
    ssize_t bytes_read;
    int buff_len = 0;

    init_sigchld_blocking();

    struct sigaction act_child;

    act_child.sa_handler = sigchld_handler;
    act_child.sa_flags = SA_RESTART;
    act_child.sa_mask = sigchld_mask;
    sigaction(SIGCHLD, &act_child, NULL);

    signal(SIGINT, SIG_IGN);

    char buff[MAX_LINE_LENGTH + 1] = {};

    while (1)
    {
        block_sigchld();
        restore_finished();
        unblock_sigchld();
        write_prompt(TERMINAL_CHECKER);
        bytes_read = read(0, buff + buff_len, MAX_LINE_LENGTH - buff_len);
        if (bytes_read < 0)
            continue;
        if (bytes_read == 0)
        {
            if (buff_len > 0)
            {
                // add /n at the end to process last line
                buff[buff_len] = '\n';
                buff_len++;
                buff[buff_len] = '\0';
            }
            else
            {
                // end of input
                break;
            }
        }

        buff_len += bytes_read;
        buff[buff_len] = '\0';
        // Null-terminate buffer

        char *start = buff;
        char *newline_pos;
        char *break_pos;

        while ((newline_pos = strchr(start, '\n')) != NULL)
        {
            // change \n to \0
            *newline_pos = '\0';
            int line_length = newline_pos - start;
            // run command if line is not empty
            if (line_length > 0 && start[0] != '\0')
            {
                block_sigchld();
                // Parse line
                ln = parseline(start);
                if (!ln)
                {
                    fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
                    start = newline_pos + 1;
                    continue;
                }
                pipelineseq *cur_pipe = ln;
                pipeline *line_start = ln->pipeline;
                do
                {
                    execute_pipeline(cur_pipe->pipeline);
                    cur_pipe = cur_pipe->next;
                } while (cur_pipe->pipeline != line_start);
                unblock_sigchld();
            }
            start = newline_pos + 1;
        }
        // move remaining part to the beginning of buffer
        int remaining_length = buff_len - (start - buff);
        if (remaining_length > 0)
        {
            memmove(buff, start, remaining_length);
        }
        buff_len = remaining_length;
        buff[buff_len] = '\0';
        // buffer overflow
        if (buff_len >= MAX_LINE_LENGTH)
        {
            fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);

            handle_buffer_overflow(buff, &buff_len);
        }
    }
    return 0;
}
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>

#include "builtins.h"

int echo(char *[]);
int undefined(char *[]);
int exit_shell(char *[]);
int my_ls(char *[]);
int my_cd(char *[]);
int my_kill(char *[]);

builtin_pair builtins_table[] = {
	{"exit", &exit_shell},
	{"lecho", &echo},
	{"lcd", &my_cd},
	{"lkill", &my_kill},
	{"lls", &my_ls},
	{NULL, NULL}};

int count_args(char *argv[])
{
	int count = 0;
	while (argv[count] != NULL)
		count++;
	return count;
}

int echo(char *argv[])
{
	int i = 1;
	if (argv[i])
		printf("%s", argv[i++]);
	while (argv[i])
		printf(" %s", argv[i++]);

	printf("\n");
	fflush(stdout);
	return 0;
}

int undefined(char *argv[])
{
	fprintf(stderr, "Command %s will exist.\n", argv[0]);
	return BUILTIN_ERROR;
}

int exit_shell(char *argv[])
{
	int argc = count_args(argv);
	if (argc != 1)
		return BUILTIN_ERROR;
	// exit shouldn't fail
	exit(0);
	// just in case exit fails
	return BUILTIN_ERROR;
}

int my_ls(char *argv[])
{
	int argc = count_args(argv);
	// no arguments
	if (argc != 1)
		return BUILTIN_ERROR;
	DIR *dir;
	struct dirent *entry;

	dir = opendir(".");
	// DIR cannot be null since '.' always exists
	while ((entry = readdir(dir)) != NULL)
	{
		if (entry->d_name[0] != '.')
			printf("%s\n", entry->d_name);
	}
	fflush(stdout);
	closedir(dir);
	return 0;
}
int my_cd(char *argv[])
{
	int argc = count_args(argv);
	// check arguments
	// exactly one argument
	if (argc == 1)
	{
		const char *home = getenv("HOME");
		if (home == NULL || chdir(home) != 0)
		{
			// no home
			return BUILTIN_ERROR;
		}
		return 0;
	}
	if (argc > 2 || chdir(argv[1]) != 0)
		return BUILTIN_ERROR;
	return 0;
}
int my_kill(char *argv[])
{
	int argc = count_args(argv);
	errno = 0;
	// used temporarily for simplicity

	// wrong number of arguments

	if (argc == 1 || argc > 3)
		return BUILTIN_ERROR;
	// check arguments
	// exactly one argument
	if (argc == 2)
	{
		// convert to integer
		char *guard;
		const int pid = strtol(argv[1] + 1, &guard, 10);
		if (errno != 0 || *guard != '\0')
			return BUILTIN_ERROR;
		// send SIGTERM
		if (kill(pid, SIGTERM) != 0)
		{
			printf("Kill failed\n");
			return BUILTIN_ERROR;
		}
		return 0;
	}
	// first argument is signal
	if (argv[1][0] != '-')
		return BUILTIN_ERROR;
	// parse signal number
	char *guard;
	const int sig = strtol(argv[1] + 1, &guard, 10);
	if (errno != 0 || *guard != '\0' || sig <= 0)
		return BUILTIN_ERROR;
	// second argument is pid
	// parse pid
	const int pid = strtol(argv[2], &guard, 10);
	if (errno != 0 || *guard != '\0' || pid <= 0)
		return BUILTIN_ERROR;
	// send signal
	if (kill(pid, sig) != 0)
	{
		return BUILTIN_ERROR;
	}
	return 0;
}
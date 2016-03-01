#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MORE_BUFF 64 //used to reallocate more space for the buffer
#define SPLITS " \t\r\n\a" //used to break up inputs


// function declarations
void shell_loop();
char *readin(); 
char **split(char *input); 
int in_or_sys(char **arguments); 
int sys_cmds(char **arguments); 
int my_cd(char **arguments); 
int my_exit(char **arguments); 
int my_status(char **arguments); 
int my_size();
void zombie_killer();
void handler(int signal);

//Declaration and implimentation of weird stuff
char *my_strings[] = {
	"cd", "status", "exit"
};

int (*my_funcs[]) (char **) = {
	&my_cd, &my_status, &my_exit
};

pid_t pidList[100] = {0};
//keeps a list of PID's

pid_t watchList[100] = {0};
//keeps a list of potential zombies, mainly any process sent to the background

int pipe_direct = 0;
//tells which direction piping is happening in
//0=none
//1 = > | output to file
//2 = < | input from STDIN

//number of processes
int num_pro = 1;

int pipe_point = 0;
//argument that is using pipe

int state = 0; 

int maxArg = 0;

int main(int argc, char **argv)
{
	/* struct sigaction sig;
	
	sigemptyset(&sig.sa_mask);
	sig.sa_flags = 0;
	sig.sa_handler = handler;
	
	printf("main");
	fflush(stdout);
	
	sigaction(SIGHUP, &sig, NULL);
	sigaction(SIGINT, &sig, NULL);
	sigaction(SIGQUIT, &sig, NULL); */
	
	shell_loop();
	
	return 0;
}

void shell_loop()
{
	char *command;
	char **arguments;
	int status;
	
	do {
		printf(": ");
		fflush(stdout);
		command = readin();
		arguments = split(command);
		status = in_or_sys(arguments);
		zombie_killer();
		
		free(command);
		free(arguments);
		
	} while (status);
}

char *readin()
{
	
	char *input = NULL; //character array for holding input
	ssize_t buffer = 0; //used to take in char 
	getline(&input, &buffer, stdin);
	return input;
}

char **split(char *input)
{
	//split_buff, buffer size for inputing and breaking things
	//i, position iterator
	int split_buff =  MORE_BUFF, i = 0;
	char **command = malloc(split_buff * sizeof(char*)); //the actual buffer, will hold broken up commands
	char *chunk; //tmp for the broken up string
	
	//##########TEST
	if(!command)
	{
		fprintf(stderr, "allocation error\n");
		exit(EXIT_FAILURE);
	}
	//##########/TEST
	
	
	chunk = strtok(input, SPLITS);
	while (chunk != NULL)
	{
		command[i] = chunk;
		i++;
		
		if (i >= split_buff)
		{
			split_buff += MORE_BUFF;
			command = realloc(command, split_buff * sizeof(char*));
			//##########TEST
			if(!command)
			{
				fprintf(stderr, "allocation error\n");
				exit(EXIT_FAILURE);
			}
			//##########/TEST
			
		}
		chunk = strtok(NULL, SPLITS);
	}
	maxArg = i - 1;
	command[i] = NULL;
	return command;
	
}

int in_or_sys(char **arguments)
{
	int i, fp;
	
	if (arguments[0] == NULL || *arguments[0] == '#') 
	{
		return 1;
	}
	
	for (i = 0; i < my_size(); i++)
	{
		if(strcmp(arguments[0], my_strings[i])  == 0 ) 
		{
			return (*my_funcs[i])(arguments);
		}
	}
	
	//checking to see if piping is involved
	if(maxArg == 2 && ((strcmp(arguments[1], ">") == 0) || (strcmp(arguments[1], "<") == 0)))
	{
		int STDOUT, STDIN;
		STDOUT = dup(0);
		STDIN = dup(1);
		
		if(strcmp(arguments[1], ">") == 0)
		{
			fp = open(arguments[2], O_WRONLY|O_CREAT|O_TRUNC, 0754);
			
			//If there is a file error
			if(fp == -1)
			{
				printf("File Error\n"); 
				state = 1;
			} else
			{
				//Change standard out to point to the opened file
				dup2(fp, 1);
				
				//prevent the redirection symbol from executing again.
				arguments[1] = NULL;
				
				close(fp);
				sys_cmds(arguments);
			}
		} else if(strcmp(arguments[1], "<") == 0)
		{
			//open file for reading
			//fp = open(arguments[2], "r", 0754);
			fp = open(arguments[2], O_RDONLY);
			
			//if there is a file error
			if(fp == -1)
			{
				printf("File Error\n");
				state = 1;
			}
			else
			{
				//point file to point to standard in
				dup2(fp, 0);
				
				
				//prevent redirection symbol from executing again.
				arguments[1] = NULL;
				
				close(fp);
				sys_cmds(arguments);
			}
		}
		//restore input / output to standard in / out
		dup2(STDOUT, 1);
		close(STDOUT);
		
		dup2(STDIN, 0);
		close(STDIN);
		return 1;
	} else
	{
		sys_cmds(arguments);
		return 1;
	}
}

int sys_cmds(char **arguments)
{
	pid_t pid, wpid, tmp;
	int status, pidHeight, i, statusus, piedPiper[2], dupTest;
	
	pid = fork();
	
	if(pid == 0) //the child | if exexcvp returns -1 something bad has happened
	{
		//checking to see if process should be run in background
		if(*arguments[maxArg] == '&')
		{
			arguments[maxArg] = NULL;
			setpgid(0, 0);
		}
		
		if(execvp(arguments[0], arguments) == -1)
		{
			perror("smallsh");
			state = 1;
		}
		exit(EXIT_FAILURE);
	} else if (pid < 0) //checks to see if there was an error in forking
	{
		perror("smallsh");
		state = 1;
	} else //handling the parent and killing the child
	{
		do{
			//putting PID of child into an array
			for(i = 0; i < 100; i++)
			{
				if(pidList[i] == 0)
				{
					pidList[i] = pid;
					break;
				}
			}
			
			//checking to see if argument is in background
			//if it is then it gets put in zombie array
			//if not then the process is handled in the foreground
			if(*arguments[maxArg] == '&')
			{
				for(i = 0; i < 100; i++)
				{
					if(watchList[i] == 0)
					{
						watchList[i] = pid;
						break;
					}
				}
				printf("background pid is %d\n", pid);
				fflush(stdout);
			} else 
			{
				wpid = waitpid(pid, &status, WUNTRACED);
				if(WIFEXITED(status))
				{
					
					state = WEXITSTATUS(status);
				} else if(WIFSIGNALED(status))
				{
					state = WTERMSIG(status);
				} else if (WIFSTOPPED(status)) 
				{
					state = WSTOPSIG(status);
				}
				pidList[i] = 0;
			}
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));
		
	}	
	return 1;
}

int my_size() 
{
	return sizeof(my_strings) / sizeof(char *);
}

int my_cd(char **arguments)
{
	if(arguments[1] == NULL) 
	{
		fprintf(stderr, "expected arguments after \"cd\"\n");
		fflush(stdout);
		state = 1;
		
	} else {
		if (chdir(arguments[1]) != 0 )
		{
			perror("smallsh");
			state = 1;
		}
		else 
		{
			state = 0;
		}
	}
	
	return 1;
}

int my_exit(char **arguments)
{
	return 0;
}

int my_status(char **arguments)
{
	printf("exit value %d\n", state);
	fflush(stdout);
	return 1;
}

void zombie_killer()
{
	pid_t zombiechild;
	int status, i;
	for(i = 0; i < 100; i++)
	{
		if(watchList[i] != 0)
		{
			while( (zombiechild = waitpid(watchList[i], &status, WNOHANG)) > 0)
			{
				if(WIFEXITED(status))
				{
					
					state = WEXITSTATUS(status);
				} else if(WIFSIGNALED(status))
				{
					state = WTERMSIG(status);
				} else if (WIFSTOPPED(status)) 
				{
					state = WSTOPSIG(status);
				}
				printf("background process pid %d is done: exit value %d\n", zombiechild, state );
				fflush(stdout);
				watchList[i] = 0;
			}
		}
	}
}

/* void handler(int signal)
{
	printf("\nENTERED HANDLER\nSIGNAL CAUGHT\n");
	int i, status, sig = 0, killed = 0;
	
	switch(signal)
	{
		case SIGQUIT:
			printf("SIG-1");
			fflush(stdout);
			sig = SIGQUIT;
			break;
		case SIGHUP:
			sig = SIGQUIT;
			printf("SIG-2");
			fflush(stdout);
			break;
		case SIGINT:
			sig = SIGINT;
			printf("SIG-3\n");
			fflush(stdout);
			for(i = 0; i < 100; i++)
			{
				if(watchList[i]!=0)
				{
					kill(watchList[i], sig);
					watchList[i] = 0;
					killed = 1;
					break;
				} else if(pidList[i]!=0)
				{
					kill(pidList[i], sig);
					pidList[i] = 0;
					killed = 1;
					break;
				}
			}
			break;
		default:
			sig = SIGQUIT;
			printf("SIG-Default");
			fflush(stdout);
	}
	//for(i = 0; i < num_pro; i++)
	//{
	//	kill(pidList[i], sig);
	//} 
	
	if(killed == 0)
	{
		exit(1);
	}
	killed = 0;
	
	shell_loop();
} */

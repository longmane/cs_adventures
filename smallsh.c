#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

//////////////////
//framework for the shell was based heavily off of 
//http://stephen-brennan.com/2015/01/16/write-a-shell-in-c/
//////////////////


#define BulkUp 64 //used to reallocate more space for the buffer
#define split_input " \t\r\n\a" //used to break up inputs


//Declaration of functions
void shell_loop();
//the main(ish) loop that runs(or ruins) everything
char *readin(); 
//reads commands in and returns them to shell_loop
char **split(char *input); 
//splits the commands into different arguments
int sys_or_intern(char **arguments); 
//decides if command is built in or a system
int SystemCommand(char **arguments); 
//runs non built in commands
int Internal_cd(char **arguments); 
//my command for changing directory
int Internal_exit(char **arguments); 
//my command for exiting
int Internal_status(char **arguments); 
//my command for status
int Internals_size();
//returns the size of my 
void Shotty();
//kills zombies
void handler(int action);

//Declaration and implimentation of weird stuff
char *Internals_string[] = {
	"cd", "status", "exit"
};

int (*Internals_function[]) (char **) = {
	&Internal_cd, &Internal_status, &Internal_exit
};

int num_pro = 1;	//Number of processes to fork

pid_t pidList[100] = {0};
//keeps a list of PID's

pid_t watchList[100] = {0};
//keeps a list of potential zombies, mainly any process sent to the background

int pipeDir = 0;
//tells which direction piping is happening in
//0=none
//1 = > | output to file
//2 = < | input from STDIN

int pipePoint = 0;
//argument that is using pipe

int stat = 0; 

int maxArg = 0;

int main(int argc, char **argv)
{

	struct sigaction sig;		//Signal handling struct

	sigemptyset(&sig.sa_mask);
	sig.sa_flags = 0;
	sig.sa_handler = handler;	//Tells the system to use my signal handler

	sigaction(SIGHUP, &sig, NULL);
	sigaction(SIGINT, &sig, NULL);
	sigaction(SIGQUIT, &sig, NULL);


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
		status = sys_or_intern(arguments);
		Shotty();
		
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
	//Machamp, buffer size for inputing and breaking things
	//i, position iterator
	int Machamp =  BulkUp, i = 0;
	char **command = malloc(Machamp * sizeof(char*)); //the actual buffer, will hold broken up commands
	char *chunk; //tmp for the broken up string
	
	//##########TEST
	if(!command)
	{
		fprintf(stderr, "allocation error\n");
		exit(EXIT_FAILURE);
	}
	//##########/TEST
	
	
	chunk = strtok(input, split_input);
	while (chunk != NULL)
	{
		command[i] = chunk;
		i++;
		
		if (i >= Machamp)
		{
			Machamp += BulkUp;
			command = realloc(command, Machamp * sizeof(char*));
			//##########TEST
			if(!command)
			{
				fprintf(stderr, "allocation error\n");
				exit(EXIT_FAILURE);
			}
			//##########/TEST
			
		}
		chunk = strtok(NULL, split_input);
	}
	maxArg = i - 1;
	command[i] = NULL;
	return command;
	
}

int sys_or_intern(char **arguments)
{
	int i;
	
	if (arguments[0] == NULL || *arguments[0] == '#') 
	{
		return 1;
	}
	
	for (i = 0; i < Internals_size(); i++)
	{
		if(strcmp(arguments[0], Internals_string[i])  == 0 ) 
		{
			return (*Internals_function[i])(arguments);
		}
	}
	
	return SystemCommand(arguments);
}

int SystemCommand(char **arguments)
{
	pid_t pid, wpid, tmp;
	int status, pidHeight, i, statusus, piedPiper[2], dupTest;
	
	//checking on Mr. Pied
	if(pipe(piedPiper) == -1)
	{
		perror("Leak in the pipes");
		exit(1);
	}
	
	//looping through to look for piping
	//starts at one under assumption that < or > won't be the first arg
	for(i = 1; i < maxArg; i++)
	{
		if(*arguments[i] == '<')
		{
			pipeDir = 2;
			pipePoint = i;
		} else if (*arguments[i] == '>')
		{
			pipeDir = 1;
			pipePoint = i;
		}
	}
	
	//
	pid = fork();
	//printf("PID: %d\n", pid);
	//fflush(stdout);
	
	if(pid == 0) //the child | if exexcvp returns -1 something bad has happened
	{
		if(*arguments[maxArg] == '&')
		{
			arguments[maxArg] = NULL;
			setpgid(0, 0);
		}
		
		if(pipeDir == 1)
		{
			close(piedPiper[0]);
		} else if(pipeDir == 2)
		{
			close(piedPiper[1]);
			arguments[pipePoint] = NULL;
			dupTest = dup2(piedPiper[0], 0);
			if(dupTest == -1)
			{
				perror("dup2");
				exit(2);
			}
		}
		
		if(execvp(arguments[0], arguments) == -1)
		{
			perror("smallsh");
			stat = 1;
		}
		exit(EXIT_FAILURE);
	} else if (pid < 0) //checks to see if there was an error in forking
	{
		perror("smallsh");
		stat = 1;
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
			
			//checks to see if any piping is occuring
			if(pipeDir == 1)
			{
				close(piedPiper[1]);
			} else if(pipeDir == 2)
			{
				close(piedPiper[0]);
				write(piedPiper[1], arguments[pipePoint+1], (strlen(arguments[pipePoint+1])+1));
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
				//printf("& Found\n");
				fflush(stdout);
				//arguments[maxArg] = NULL;
			} else 
			{
				wpid = waitpid(pid, &status, WUNTRACED);
				if(WIFEXITED(status))
				{
					
					stat = WEXITSTATUS(status);
				} else if(WIFSIGNALED(status))
				{
					stat = WTERMSIG(status);
				} else if (WIFSTOPPED(status)) 
				{
					stat = WSTOPSIG(status);
				}
			}
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));
		
	}
	////////////////////////////////////
	//PID TESTING
	/* for(i = 0; i < 100; i++)
	{
		if(pidList[i] != 0)
		{
			tmp = pidList[i];
			printf("PID: %d\n", tmp);
			fflush(stdout);
		} else {
			break;
		}
	} */
	////////////////////////////////////
	
	return 1;
}

int Internals_size() 
{
	return sizeof(Internals_string) / sizeof(char *);
}

int Internal_cd(char **arguments)
{
	if(arguments[1] == NULL) 
	{
		fprintf(stderr, "expected arguments to \"cd\"\n");
		fflush(stdout);
		stat = 1;
		
	} else {
		if (chdir(arguments[1]) != 0 )
		{
			perror("smallsh");
			stat = 1;
		}
		else 
		{
			stat = 0;
		}
	}
	
	return 1;
}

int Internal_exit(char **arguments)
{
	return 0;
}

int Internal_status(char **arguments)
{
	//printf("Status? Dope\n");
	printf("exit value %d\n", stat);
	fflush(stdout);
	return 1;
}

void Zombie_Status()
{
	
}

void Shotty()
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
					
					stat = WEXITSTATUS(status);
				} else if(WIFSIGNALED(status))
				{
					stat = WTERMSIG(status);
				} else if (WIFSTOPPED(status)) 
				{
					stat = WSTOPSIG(status);
				}
				printf("background pid %d is done: exit value %d\n", zombiechild, stat);
			}
		}
	}
}

//Waits for all of the children to finish
void burn_kids() {
	int i, status;
	for (i = 0; i < num_pro; ++i) {
		wait(NULL);
	}
}

//Sends signals to all of the offspring.
void handler(int action) {
	printf("\nENTERED HANDLER\n SIGNAL CAUGHT\n");
	int signal = 0;
	int i;

	switch (action) {
	case SIGQUIT:
		signal = SIGQUIT;
		break;
	case SIGHUP:
		signal = SIGQUIT;
		break;
	case SIGINT:
		signal = SIGINT;
		break;
	default:
		signal = SIGQUIT;
	}
	for (i = 0; i < num_pro; ++i) {
		kill(pidList[i], signal);
	}

	//Kill all of the kids
	burn_kids();

	//free(pidList);

	exit(1);
}

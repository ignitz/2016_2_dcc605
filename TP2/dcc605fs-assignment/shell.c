#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "fs.h"

#define FILE_NAME "img"

// Command
#define CMD_CD 1
#define CMD_MK 2
#define CMD_RM 3
#define CMD_MKDIR 4
#define CMD_RMDIR 5
#define CMD_LS 6
#define CMD_PWD 7

int get_last_char(char * text, char c) {
	int i = 0;
	int ret = 0;

	while(text[i] != '\0') {
		if (text[i] == c)
			ret = i;
		i++;
	}
	return ret;
}

void gen_file() {

	int fsize = 1 << 20;

	char *buf = malloc(fsize);

	if(!buf) { perror(NULL); exit(EXIT_FAILURE); }
	memset(buf, 0, fsize);

	unlink(FILE_NAME);
	FILE *fd = fopen(FILE_NAME, "w");
	fwrite(buf, 1, fsize, fd);
	fclose(fd);
}

void gen_init_files(struct superblock * sb) {
	char * temp = malloc(255*sizeof(char));

	fs_mkdir(sb, "/usr");
	fs_mkdir(sb, "/usr/yuri");
	fs_mkdir(sb, "/usr/yuri/Desktop");
	// fs_write_file(sb, "/usr/yuri/Desktop/someFile", char *buf,
 //        size_t cnt)

	fs_mkdir(sb, "/bin");
	fs_mkdir(sb, "/dev");
	fs_mkdir(sb, "/var");
}

int get_command(char * pch) {
	if (strcmp(pch, "cd") == 0)
	{
		return CMD_CD;
	}
	else if (strcmp(pch, "mk") == 0)
	{
		return CMD_MK;
	}
	else if (strcmp(pch, "rm") == 0)
	{
		return CMD_RM;
	}
	else if (strcmp(pch, "mkdir") == 0)
	{
		return CMD_MKDIR;
	}
	else if (strcmp(pch, "rmdir") == 0)
	{
		return CMD_RMDIR;
	}
	else if (strcmp(pch, "ls") == 0)
	{
		return CMD_LS;
	}
	else if (strcmp(pch, "pwd") == 0) {
		return CMD_PWD;
	} 

	return NULL;

}

int main(int argc, char const *argv[])
{
	char * input = malloc(255*sizeof(char));
	char * pch;
	char * temp;
	char * list; // List DIR

	char * path = malloc(255*sizeof(char));
	strcpy(path, "/");

	// gen_file();
	struct superblock *sb = fs_open(FILE_NAME);
	// sb = fs_format(FILE_NAME, 128);

	// Here you put the struct dir + file to test
	// gen_init_files(sb);

	printf("\nWelcome to ShellFS Porcão!\n");
	printf("List of commands:\n");
	printf("\tcd [path]\n");
	printf("\tls\n");
	printf("\tmk [file] [string_data]\n");
	printf("\trm [file]\n");
	printf("\tmkdir [path]\n");
	printf("\trmdir [path]\n");
	printf("\texit\n");


	while(strcmp(input, "exit")) {
		printf("%s$ ", path);
		gets(input);
		pch = strtok(input, " ");

		if (pch == NULL)
			continue;

		switch(get_command(pch)) {
			case CMD_CD:
				pch = strtok(NULL, " ");
				if (strcmp("..", pch) == 0) {
					temp = malloc(255*sizeof(char));
					strcpy(temp, path);
					strncpy(path, temp, get_last_char(path, '/'));
					path[get_last_char(path, '/') + 1] = '\0';
					free(temp);
				}
				else if (strcmp(".", pch) == 0) {
					// stay in dir
				}
				else {
					// change dir
					// TODO
					// check if path exist
					
					strcat(path, pch);
					printf("Change path to %s\n", path);
				}
				break;
			case CMD_MK:
				pch = strtok(NULL, " ");
				temp = malloc(255*sizeof(char));
				strcpy(temp, pch);
				pch = strtok(NULL, " ");
				if (pch) {
					if (fs_write_file(sb, temp, pch, strlen(pch)))
						printf("ERRO em criar arquivo %s\n", temp);
				}
				
				free(temp);
				break;
			case CMD_RM:
				pch = strtok(NULL, " ");
				if (fs_unlink(sb, pch))
					printf("ERRO em apagar arquivo %s\n", pch);
				break;
			case CMD_MKDIR:
				pch = strtok(NULL, " ");
				if (fs_mkdir(sb, pch))
					printf("ERRO em criar diretório %s\n", pch);;
				break;
			case CMD_RMDIR:
				pch = strtok(NULL, " ");
				if (fs_rmdir(sb, pch))
					printf("ERRO em apagar diretório %s\n", pch);;
				break;
			case CMD_LS:
				pch = strtok(NULL, " ");
				list = fs_list_dir(sb, path);
				printf("%s\n", list);
				free(list);
				break;
			case CMD_PWD:
				printf("%s\n", path);
				break;
			default:
				printf("Command inválid\n");
				break;
		}
	}

	free(input);
	free(path);

	fs_close(sb);
	
	return 0;
}

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "fs.h"

static char *fname = "img";

// Command

void generate_file(uint64_t fsize)/*{{{*/
{
	char *buf = malloc(fsize);
	if(!buf) { perror(NULL); exit(EXIT_FAILURE); }
	memset(buf, 0, fsize);
	unlink("img");
	FILE *fd = fopen("img", "w");
	fwrite(buf, 1, fsize, fd);
	fclose(fd);
}
/*}}}*/

// insert file in img file
int insert_files(struct superblock * sb) {
	printf("DEBUG: Inserting files\n");

	char * file_name = NULL;
	char * file_path ;
	char *buffer;
	size_t len = 0;
	long file_len = 0;

	FILE *fList = fopen("files/list.txt", "r");
	FILE *fp;

	// This is manual, create directory
	fs_mkdir(sb, "/bin");
	fs_mkdir(sb, "/home");
	fs_mkdir(sb, "/home/yuri");
	fs_mkdir(sb, "/home/cassios");

	while(getline(&file_name, &len, fList) != -1) {
		file_name[strlen(file_name) - 1] = '\0';

		file_path = malloc((strlen(file_name) + strlen("files"))*sizeof(char));		
		strcpy(file_path, "files");
		strcat(file_path, file_name);

		fp = fopen(file_path, "rb");
		free(file_path);
		
		fseek(fp, 0, SEEK_END);
		file_len = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		if (file_len == -1) {
			fprintf(stderr, "DEBUG: ERROR in file size");
			return -1;
		}

		buffer = malloc(file_len);
		fread(buffer, 1, file_len, fp);
		printf("DEBUG: file_name: %s\n", file_name);
		fs_write_file(sb, file_name, buffer, file_len);

		fclose(fp);
	}

	fclose(fList);
	return 0;
}

// insert file in img file
int extract_files(struct superblock * sb) {
	printf("DEBUG: Extracting files \"extract_files\"\n");

	char * file_name = NULL;
	size_t len = 0;
	char * file_path;

	char * buffer;
	long file_len = 0;

	struct inode* file_node;
	struct nodeinfo* file_info;



	FILE *fList = fopen("files/list.txt", "r");
	FILE *fp;

	// check if directory structure is OK
	// XGH mode, sorry
	// mkdir("extract/", 0x666);
	// mkdir("extract/bin", 0x666);
	// mkdir("extract/home", 0x666);
	// mkdir("extract/home/yuri", 0x666);
	// mkdir("extract/cassios/yuri", 0x666);
	// XGH end
	
	while(getline(&file_name, &len, fList) != -1) {
		printf("-------------------------\n");
		printf("DEBUG: Extracting %s\n", file_name);

		file_name[strlen(file_name) - 1] = '\0';

		file_path = malloc((strlen(file_name) + strlen("extract"))*sizeof(char));		
		strcpy(file_path, "extract");
		strcat(file_path, file_name);
		
		file_len = get_file_size(sb, file_name);
		
		if (file_len == -1) {
			fprintf(stderr, "ERROR: file %s does not exist", file_name);
			return -1;
		}

		buffer = (char*) malloc(file_len * sizeof(char));

		file_len = fs_read_file(sb, file_name, buffer, file_len);
		if (file_len == -1) {
			fprintf(stderr, "ERROR: ERROR in file size");
			return -1;
		}


		FILE *fp = fopen(file_path, "wb");

		fwrite(buffer, 1, file_len, fp);
		fclose(fp);
	    
		free(file_path);
		// Logo abaixo da erro se o nome do arquivo tiver extensão "ex: .zip"
		free(buffer);
	}

	fclose(fList);
	return 0;
}

#define ERROR(str) { puts(str); return -1; }
int main(int argc, char const *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "give 2 or more args");
		return EXIT_FAILURE;
	}

	uint64_t fsize;
	uint64_t blksz;
	struct superblock * sb;
	int err;

	int choice = atoi(argv[1]);

	switch(choice) {
		case 0: // Generate file
			if (argc != 4) return -1;
			fsize = 1 << strtoul(argv[2], NULL, 0);
			blksz = strtoul(argv[3], NULL, 0);

			generate_file(fsize);

			sb = fs_open("img");

			printf("DEBUG: FORMAT C: \n");
			printf(
				"DEBUG: return pointer for sb = %d\n",
				sb = fs_format("img", blksz)
				);
			err = errno;
			printf("DEBUG: errno = %d\n", err);
			if(blksz < MIN_BLOCK_SIZE) {
				if(err != EINVAL) ERROR("FAIL did not set errno\n");
				if(sb != NULL) ERROR("FAIL formatted too small blocks\n");
				return 0;
			}
			if(fsize/blksz < MIN_BLOCK_COUNT) {
				if(err != ENOSPC) ERROR("FAIL did not set errno\n");
				if(sb != NULL) ERROR("FAIL formatted too small volume\n");
				return 0;
			}
			if(sb == NULL) ERROR("FAIL no sb\n");

			fs_close(sb);
			break;
		case 1: // Insert file
			sb = fs_open("img");

			if (insert_files(sb) != 0 && sb == 0) {
				fprintf(stderr, "ERROR: Unknow ERROR\n");
				fprintf(stderr, "DEBUG: errno = %d\n", errno);
				return EXIT_FAILURE;
			}
			
			fs_close(sb);
			break;
		case 2: // extract file
			sb = fs_open("img");

			if (extract_files(sb) != 0 && sb == 0) {
				fprintf(stderr, "ERROR: Unknow ERROR\n");
				fprintf(stderr, "DEBUG: errno = %d\n", errno);
				return EXIT_FAILURE;
			}

			fs_close(sb);
			break;
		case 3: // Diff
			
			break;
		case 4:
			
			break;
		case 5:
			
			break;
		default:
			fprintf(stderr, "invalid command");
	}
	
	return 0;
}

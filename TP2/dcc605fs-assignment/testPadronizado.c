#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "fs.h"

void test(uint64_t fsize, uint64_t blksz);
void fs_check(const struct superblock *sb, uint64_t fsize, uint64_t blksz);
void fs_free_check(struct superblock **sb, uint64_t fsize, uint64_t blksz);

#define NELEMS(x) (sizeof(x)/sizeof(x[0]))

static char *fname = "img";

int main(int argc, char **argv)
{
	uint64_t fsizes[] = {1 << 19, 1 << 20, 1 << 21, 1 << 23, 1 << 25};
	uint64_t blkszs[] = {64, 128, 256, 512, 1024};
	int i;

	for(i = 0; i < NELEMS(blkszs); i++) {
		printf("fsize %d blksz %d\n", (int)fsizes[i], (int)blkszs[i]);
		test(fsizes[i], blkszs[i]);
	}



	exit(EXIT_SUCCESS);
}

void test(uint64_t fsize, uint64_t blksz)
{
	int err;

	char *buf = malloc(fsize);
	if(!buf) { perror(NULL); exit(EXIT_FAILURE); }
	memset(buf, 0, fsize);

	unlink(fname);
	FILE *fd = fopen(fname, "w");
	fwrite(buf, 1, fsize, fd);
	fclose(fd);

	struct superblock *sb = fs_open(fname);
	if(errno != EBADF) { printf("FAIL did not set errno\n"); }
	if(sb != NULL) { printf("FAIL unformatted img\n"); }

	sb = fs_format(fname, blksz);
	err = errno;
	if(blksz < MIN_BLOCK_SIZE) {
		if(err != EINVAL) printf("FAIL did not set errno\n");
		if(sb != NULL) printf("FAIL formatted too small blocks\n");
	}
	if(fsize/blksz < MIN_BLOCK_COUNT) {
		if(err != ENOSPC) printf("FAIL did not set errno\n");
		if(sb != NULL) printf("FAIL formatted too small volume\n");
	}

	if(sb == NULL) return;

	fs_check(sb, fsize, blksz);
	fs_free_check(&sb, fsize, blksz);
	fs_check(sb, fsize, blksz);

	if(fs_close(sb)) perror("format_close");

	sb = fs_open(fname);
	if(!sb) perror("open");

	fs_check(sb, fsize, blksz);
	fs_free_check(&sb, fsize, blksz);
	fs_check(sb, fsize, blksz);

	if(fs_open(fname)) {
		printf("FAIL opened FS twice\n");
	} else if(errno != EBUSY) {
		printf("FAIL did not set errno EBUSY on fs reopen\n");
	}

	if(fs_close(sb)) perror("open_close");
}

void fs_free_check(struct superblock **sb, uint64_t fsize, uint64_t blksz)
{
	long long numblocks = fsize/blksz - (*sb)->freeblks;
	unsigned long long freeblks = (*sb)->freeblks;

	if(numblocks > 6) {
		printf("FAIL used more than 6 blocks on empty fs\n");
	}

	numblocks = fsize/blksz;
	
	char *blkmap = malloc(fsize/blksz);
	if(!blkmap) { perror(NULL); exit(EXIT_FAILURE); }
	memset(blkmap, 0, fsize/blksz);

	struct freepage *fp = malloc(blksz);
	if(!fp) { perror(NULL); exit(EXIT_FAILURE); }

	lseek((*sb)->fd, (*sb)->freelist * blksz, SEEK_SET);
	read((*sb)->fd, fp, blksz);

	uint64_t blknum = fs_get_block(*sb);
	while(blknum != 0 && blknum != ((uint64_t)-1)) {
		unsigned long long llu = blknum;
		if(blknum >= numblocks) {
			printf("FAIL blknum %llu >= numblocks %llu\n", llu, numblocks);
		} else if(blkmap[blknum] != 0) {
			printf("FAIL blknum %llu returned before\n", llu);
		} else {
			blkmap[blknum] = 1;
		}
		blknum = fs_get_block(*sb);
	}
	if((*sb)->freeblks != 0) printf("FAIL sb->freeblks != 0\n");

	fs_close(*sb);
	*sb = fs_open(fname);
	if((*sb)->freeblks != 0) printf("FAIL reopen sb->freeblks != 0\n");

	for(uint64_t i = 0; i < numblocks; i++) {
		if(!blkmap[i]) continue;
		fs_put_block(*sb, i);
	}
	if((*sb)->freeblks != freeblks) printf("FAIL sb->freeblks != freeblks\n");
}

void fs_check(const struct superblock *sb, uint64_t fsize, uint64_t blksz)
{
	if(sb->magic != 0xdcc605f5) { printf("FAIL magic\n"); }	
	if(sb->blks != fsize/blksz) { printf("FAIL number of blocks\n"); }
	if(sb->blksz != blksz) { printf("FAIL block size\n"); }

	struct inode *inode = malloc(blksz);
	if(!inode) { perror(NULL); exit(EXIT_FAILURE); }

	lseek(sb->fd, sb->root * blksz, SEEK_SET);
	read(sb->fd, inode, blksz);
/*
    printf("debug\n\n");
    printf("parent: %" PRIu64 "\n", inode->parent);
    printf("meta: %" PRIu64 "\n", inode->meta);
    printf("next: %" PRIu64 "\n", inode->next);
    printf("mode: %" PRIu64 "\n", inode->mode);
    */
	if(inode->mode != IMDIR) { printf("FAIL root IMDIR\n"); }
	if(inode->next != 0) { printf("FAIL root next\n"); }

	struct nodeinfo *info = malloc(blksz);
	if(!info) { perror(NULL); exit(EXIT_FAILURE); }

	lseek(sb->fd, inode->meta * blksz, SEEK_SET);
	read(sb->fd, info, blksz);

	if(info->size != 0) { printf("FAIL root size\n"); }
	if(info->name[0] != '/' || info->name[1] != '\0') { printf("FAIL root name\n"); }
	
	free(info);
	free(inode);
}

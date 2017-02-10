#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>
#include <assert.h>

#include "fs.h"

struct inode* retrieve_inode(struct superblock* sb, uint64_t block);
struct nodeinfo* retrieve_nodeinfo(struct superblock* sb, uint64_t block);
struct freepage* retrieve_freepage(struct superblock* sb, uint64_t block);
void save_superblock(struct superblock* sb);
void save_inode(struct superblock* sb, struct inode* node, uint64_t block);
void save_nodeinfo(struct superblock* sb, struct nodeinfo* ni, uint64_t block);
void save_freepage(struct superblock* sb, struct freepage* fp, uint64_t block);
uint64_t get_inode_block(struct superblock *sb, const char *full_path, int *full_match, char* path_left_over);
int get_max_links_in_node(struct superblock* sb);
int get_num_links_in_node(struct inode* node);
uint64_t get_last_inode(struct superblock* sb, uint64_t block);
uint64_t get_node_blk_with_space(struct superblock* sb, uint64_t block);
void link_node_to_nodelist(struct superblock* sb, uint64_t ref_blk, uint64_t blk_to_link, uint64_t nbytes);
void free_file_data_blocks(struct superblock* sb, uint64_t file_block);
uint64_t create_entity(struct superblock* sb, uint64_t parent_blk, const char* ename, uint64_t mode);
void write_to_file(struct superblock *sb, uint64_t file_blk, char *buf, size_t buf_sz); 
void unlink_node(struct superblock* sb, uint64_t dir_blk, uint64_t blk_to_unlink);
uint64_t get_file_size(struct superblock *sb, const char *fname);

/* Build a new filesystem image in =fname (the file =fname should be present
 * in the OS's filesystem).  The new filesystem should use =blocksize as its
 * block size; the number of blocks in the filesystem will be automatically
 * computed from the file size.  The filesystem will be initialized with an
 * empty root directory.  This function returns NULL on error and sets errno
 * to the appropriate error code.  If the block size is smaller than
 * MIN_BLOCK_SIZE bytes, then the format fails and the function sets errno to
 * EINVAL.  If there is insufficient space to store MIN_BLOCK_COUNT blocks in
 * =fname, then the function fails and sets errno to ENOSPC. */
struct superblock * fs_format(const char *fname, uint64_t blocksize) {
    if (blocksize < MIN_BLOCK_SIZE) {
        errno = EINVAL;
        return NULL;
    }

    //opens and locks the file. It prevents the file being open more than once,
    //avoid data corruption. LOCK_NB makes it a nonblocking request, so we get the
    //error instead of keep waiting for the look to be released
    int fd = open(fname, O_RDWR);
    if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
        errno = EBUSY;
        return NULL;
    }

    struct stat fileStat;
    if (fstat(fd, &fileStat) < 0) {
        printf("**fs_format** Error: could not get file status");
        exit(-1);
    }
    uint64_t no_blocks = fileStat.st_size / blocksize;
    
    if (no_blocks < MIN_BLOCK_COUNT) {
        close(fd);
        errno = ENOSPC;
        return NULL;
    }

    /* block 0 -> superblock
       block 1 -> root directory
       block 2 -> metadata (nodeinfo) of the root directory */	

    struct superblock *sb = (struct superblock*) calloc(1, blocksize);
    sb->magic = 0xdcc605f5;
    sb->blks = no_blocks;
    sb->blksz = blocksize;
    sb->root = 1; //pointer to the block that contains the root directory
    sb->freelist = 3;
    sb->freeblks = sb->blks - 4;
    sb->fd = fd;
    save_superblock(sb);

    struct inode *root_dir = (struct inode*) calloc(1, blocksize);
    root_dir->mode = IMDIR;
    root_dir->parent = 1;
    root_dir->meta = 2;
    root_dir->next = 0; 
    save_inode(sb, root_dir, sb->root); 

    struct nodeinfo *info = (struct nodeinfo*) calloc(1, blocksize);
    info->size = 0;
    info->name[0] = '/';
    info->name[1] = '\0';
    save_nodeinfo(sb, info, root_dir->meta); 

    struct freepage* fp = (struct freepage*) calloc(1, blocksize);
	for (uint64_t cnt = sb->freelist; cnt < sb->blks; cnt++) {
        fp->next = (cnt + 1 == sb->blks) ? 0 : cnt + 1;
        save_freepage(sb, fp, cnt);
    }
    
	free(info);
    free(fp);
    free(root_dir);

    return sb;
}

/* Open the filesystem in =fname and return its superblock.  Returns NULL on
 * error, and sets errno accordingly.  If =fname does not contain a
 * 0xdcc605fs, then errno is set to EBADF. */
struct superblock * fs_open(const char *fname) {
    //opens and locks the file. It prevents the file being open more than once,
    //avoid data corruption. LOCK_NB makes it a nonblocking request, so we get the
    //error instead of keep waiting for the look to be released
    int fd = open(fname, O_RDWR);
    if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
        errno = EBUSY;
        return NULL;
    }

    //read part of the block from disk. Note that probably
    //blocksize > sizeof(struct superblock), so a reallocation
    //is going to be done bellow
    struct superblock* sb = (struct superblock*) malloc(sizeof (struct superblock));
    lseek(fd, 0, SEEK_SET);
    read(fd, sb, sizeof(struct superblock));

    //check if file was formated (look for dcc code)	
    if (sb->magic != 0xdcc605f5) {
        close(fd);
        free(sb);
        errno = EBADF;
        return NULL;
    }

    //After find out the block size for this file,
    //read the full block and write it back to save =fd
    sb = (struct superblock*) realloc(sb, sb->blksz);
    lseek(fd, 0, SEEK_SET);
    read(fd, sb, sb->blksz);
    
    sb->fd = fd;
    lseek(fd, 0, SEEK_SET);
    write(fd, sb, sb->blksz);

    return sb;
}

/* Close the filesystem pointed to by =sb.  Returns zero on success and a
 * negative number on error.  If there is an error, all resources are freed
 * and errno is set appropriately.  If =fname does not contain a
 * 0xdcc605fs, then errno is set to EBADF. */
int fs_close(struct superblock *sb) {
    if (sb == NULL) {
        printf("**fs_close** Error: received a NULL superblock\n");
        return -1;
    }

    //check if file was formated (look for dcc code)	
    if (sb->magic != 0xdcc605f5) {
        free(sb);
        errno = EBADF;
        return -1;
    }
    save_superblock(sb);

    close(sb->fd);
    free(sb);
    return 0;
}

/* Get a free block in the filesystem.  This block shall be removed from the
 * list of free blocks in the filesystem.  If there are no free blocks, zero
 * is returned.  If an error occurs, (uint64_t)-1 is returned and errno is set
 * appropriately. */
uint64_t fs_get_block(struct superblock *sb) {
    if (sb->freeblks == 0) {
        return 0;
    }

    uint64_t block = sb->freelist;
    struct freepage* fp = retrieve_freepage(sb, sb->freelist);
    sb->freelist = fp->next;
    sb->freeblks--;
    
    save_superblock(sb);
    free(fp);
    return block;
}

/* Put =block back into the filesystem as a free block.  Returns zero on
 * success or a negative value on error.  If there is an error, errno is set
 * accordingly. */
int fs_put_block(struct superblock *sb, uint64_t block) {

    struct freepage* fp = (struct freepage*) calloc(1, sb->blksz);
    fp->next = sb->freelist;
    save_freepage(sb, fp, block);
    
    sb->freelist = block;
    sb->freeblks++;
    save_superblock(sb);

    return 0;
}

int fs_write_file(struct superblock *sb, const char *fname, char *buf,
        size_t cnt) {

    uint64_t num_blocks = cnt / sb->blksz;
    if (num_blocks == 0) num_blocks++;
    if (num_blocks > sb->freeblks) {
        errno = ENOSPC;
        return -1;
    }

    int full_match = 0;
    char file_short_name[50] = "";
    uint64_t dir_or_file_blk = get_inode_block(sb, fname, &full_match, file_short_name);
    
    if (full_match == 0) {
        //file does not exist, so it has to be created
        dir_or_file_blk = create_entity(sb, dir_or_file_blk, file_short_name, IMREG);
    } else {
        //file already exists, so it will be overrided
        free_file_data_blocks(sb, dir_or_file_blk);
    }
    write_to_file(sb, dir_or_file_blk, buf, cnt);

    return 0;
}

ssize_t fs_read_file(struct superblock *sb, const char *fname, char *buf,
        size_t bufsz) {
    // TODO create a limit with bufsz
    
    int full_match = 0;
    uint64_t file_blk = get_inode_block(sb, fname, &full_match, NULL);
    
    if (full_match == 0) {
        //file does not exist
        errno = ENOENT;
        return -1;
    }

    struct inode* file_node = retrieve_inode(sb, file_blk);
    struct nodeinfo* file_info = retrieve_nodeinfo(sb, file_node->meta);
 
    uint64_t filesz = file_info->size;
    // Must be started at zero when the file is too small
    uint64_t ii = 0; 
    uint64_t cnt_bufsz = 0;

    while (cnt_bufsz + sb->blksz < filesz) {
        for (ii = 0; file_node->links[ii] != 0 && cnt_bufsz + sb->blksz < filesz; ii++) {
            uint64_t blk = file_node->links[ii];
            lseek(sb->fd, blk * sb->blksz, SEEK_SET);
	        read(sb->fd, buf, sb->blksz);
            cnt_bufsz += sb->blksz;
            buf += sb->blksz;
        }
        if (file_node->next != 0) {
            lseek(sb->fd, file_node->next * sb->blksz, SEEK_SET);
	        read(sb->fd, file_node, sb->blksz);
            ii = 0; //corner case: the remaining data is on the next node
        }
    }

    //remaining data
    size_t left_over = filesz - cnt_bufsz;
    if (left_over != 0) {
        uint64_t blk = file_node->links[ii];
        lseek(sb->fd, blk * sb->blksz, SEEK_SET);
        read(sb->fd, buf, left_over);
        cnt_bufsz += left_over;
    }
    
    free(file_node);
    free(file_info);

    return cnt_bufsz;
}

int fs_unlink(struct superblock *sb, const char *fname) {
    int full_match = 0;
    uint64_t file_blk = get_inode_block(sb, fname, &full_match, NULL);
    
    if (full_match == 0) {
        //file does not exist
        errno = ENOENT;
        return -1;
    }
    
    free_file_data_blocks(sb, file_blk);

    struct inode* file_node = retrieve_inode(sb, file_blk);
    assert(file_node->next == 0);

    //update parent dir
    unlink_node(sb, file_node->parent, file_blk);

    //remove inode root and metadata
    fs_put_block(sb, file_node->meta);
    fs_put_block(sb, file_blk);

    free(file_node);
    return 0;
}

int fs_mkdir(struct superblock *sb, const char *dname) {
    int full_match = 0;
    char dir_short_name[50];
    uint64_t parent_dir_blk = get_inode_block(sb, dname, &full_match, dir_short_name);
    
    if (full_match == 1) {
        //dir already exists
        errno = EEXIST;
        return -1;
    }
    create_entity(sb, parent_dir_blk, dir_short_name, IMDIR);
    return 0;
}

int fs_rmdir(struct superblock *sb, const char *dname) {
    int full_match = 0;
    uint64_t dir_blk = get_inode_block(sb, dname, &full_match, NULL);
    
    if (full_match == 0) {
        //dir does not exist
        errno = ENOENT;
        return -1;
    }
    struct inode* dir_node = retrieve_inode(sb, dir_blk);
    
    if (dir_node->mode != IMDIR) {
        //node is a file
        free(dir_node);
        errno = ENOTDIR;
        return -1;
    }
    
    struct nodeinfo* dir_info = retrieve_nodeinfo(sb, dir_node->meta);
    if (dir_info->size != 0) {
        //node is a file
        free(dir_node);
        free(dir_info);
        errno = ENOTEMPTY;
        return -1;
    }
    assert(dir_node->next == 0);

    //update parent dir
    unlink_node(sb, dir_node->parent, dir_blk);

    //remove node and metadata
    fs_put_block(sb, dir_node->meta);
    fs_put_block(sb, dir_blk);

    free(dir_node);
    free(dir_info);
    return 0;
}

char * fs_list_dir(struct superblock *sb, const char *dname) {
    int full_match = 0;
    uint64_t dir_blk = get_inode_block(sb, dname, &full_match, NULL);
    
    if (full_match == 0) {
        //dir does not exist
        errno = ENOENT;
        return NULL;
    }
    struct inode* dir_node = retrieve_inode(sb, dir_blk);
    
    if (dir_node->mode != IMDIR) {
        //node is a file
        free(dir_node);
        errno = ENOTDIR;
        return NULL;
    }
    
    struct nodeinfo* dir_info = retrieve_nodeinfo(sb, dir_node->meta);

    struct inode* ent_node = (struct inode*) calloc(1, sb->blksz); 
    struct nodeinfo* ent_info = (struct nodeinfo*) calloc(1, sb->blksz); 
    struct inode *curr_node = (struct inode*) calloc(1, sb->blksz);
    uint64_t curr_blk = dir_blk;
   
    //TODO: realloc array dynamically
    char* list = (char*) malloc(1000 * sizeof(char*));
    strcpy(list, "");
    const uint64_t nelem_in_dir = dir_info->size;
    uint64_t cnt = 0;

iterate_over_inode:
    lseek(sb->fd, curr_blk * sb->blksz, SEEK_SET);
    read(sb->fd, curr_node, sb->blksz);
    
    for (int index = 0; curr_node->links[index] != 0; index++) {
        lseek(sb->fd, curr_node->links[index] * sb->blksz, SEEK_SET);
        read(sb->fd, ent_node, sb->blksz);

        lseek(sb->fd, ent_node->meta * sb->blksz, SEEK_SET);
        read(sb->fd, ent_info, sb->blksz);
        
        strcat(list, ent_info->name);
        if (ent_node->mode == IMDIR) {
            strcat(list, "/ ");
        } else {
            strcat(list, " ");
        }
        cnt++;
    }

    //go to next inode in list
    if (curr_node->next != 0) {
        assert (cnt < nelem_in_dir);
        curr_blk = curr_node->next;
        goto iterate_over_inode;
    }
    
    assert (cnt == nelem_in_dir);

    list[strlen(list) -1] = '\0'; 
    free(dir_node);
    free(dir_info);
    free(ent_node);
    free(ent_info);
    free(curr_node);
    return list;
}

struct inode* retrieve_inode(struct superblock* sb, uint64_t block) {
    struct inode* node = (struct inode*) calloc(1, sb->blksz);
    lseek(sb->fd, block * sb->blksz, SEEK_SET);
	read(sb->fd, node, sb->blksz);
    return node;
}

struct nodeinfo* retrieve_nodeinfo(struct superblock* sb, uint64_t block) {
    struct nodeinfo* nodeinfo = (struct nodeinfo*) calloc(1, sb->blksz);
    lseek(sb->fd, block * sb->blksz, SEEK_SET);
	read(sb->fd, nodeinfo, sb->blksz);
    return nodeinfo;
}

struct freepage* retrieve_freepage(struct superblock* sb, uint64_t block) {
    struct freepage* fp = (struct freepage*) calloc(1, sb->blksz);
    lseek(sb->fd, block * sb->blksz, SEEK_SET);
	read(sb->fd, fp, sb->blksz);
    return fp;
}

void save_superblock(struct superblock* sb) {
    lseek(sb->fd, 0, SEEK_SET);
	write(sb->fd, sb, sb->blksz);
}

void save_inode(struct superblock* sb, struct inode* node, uint64_t block) {
    lseek(sb->fd, block * sb->blksz, SEEK_SET);
	write(sb->fd, node, sb->blksz);
}

void save_nodeinfo(struct superblock* sb, struct nodeinfo* ni, uint64_t block) {
    lseek(sb->fd, block * sb->blksz, SEEK_SET);
	write(sb->fd, ni, sb->blksz);
}

void save_freepage(struct superblock* sb, struct freepage* fp, uint64_t block) {
    lseek(sb->fd, block * sb->blksz, SEEK_SET);
	write(sb->fd, fp, sb->blksz);
}


/* This function returns the block number for the deepest matching token in the path
 * If the path was fully matched, it sets full_match to 1, otherwise 0 */
uint64_t get_inode_block(struct superblock *sb, const char *full_path, int *full_match, char* path_left_over) {
    *full_match = 1;
    if (strcmp("/", full_path) == 0) {
        return sb->root;
    }

    struct inode *curr_node = (struct inode*) calloc(1, sb->blksz);
    struct inode *subdir_or_file_node = (struct inode*) calloc(1, sb->blksz);
    struct nodeinfo *nodeinfo = (struct nodeinfo*) calloc(1, sb->blksz);

    //starts from the root
    uint64_t curr_block = sb->root;
    lseek(sb->fd, curr_block * sb->blksz, SEEK_SET);
    read(sb->fd, curr_node, sb->blksz);

    //for each token in the fullpath
    //  look for its block
    int token_matched = 0;
    char* path = malloc(strlen(full_path) * sizeof(char));
    strcpy(path, full_path);
    char * pch;
    for (pch = strtok(path, "/"); pch != NULL; pch = strtok(NULL, "/")) {
        // printf ("Path token: %s\n", pch);
        token_matched = 0;

        //check if node is in curr_node linked list
        //if not and curr_node->next != 0
        //  go to next inode
search_node_links:
        for(int link_index = 0; curr_node->links[link_index] != 0; link_index++) {
            lseek(sb->fd, curr_node->links[link_index] * sb->blksz, SEEK_SET);
            read(sb->fd, subdir_or_file_node, sb->blksz);

            lseek(sb->fd, subdir_or_file_node->meta * sb->blksz, SEEK_SET);
            read(sb->fd, nodeinfo, sb->blksz);

            // printf ("nodeinfo->name: %s\n", nodeinfo->name);
            if (strcmp(nodeinfo->name, pch) == 0) {
                token_matched = 1;

                //goes down one level
                curr_block = curr_node->links[link_index];
                lseek(sb->fd, curr_node->links[link_index] * sb->blksz, SEEK_SET);
                read(sb->fd, curr_node, sb->blksz);
                break;
            }
        }
        
        if (token_matched == 1) continue;

        if (curr_node->next != 0) {
            lseek(sb->fd, curr_node->next, SEEK_SET);
            read(sb->fd, curr_node, sb->blksz);
            goto search_node_links;
        } else {
            break; //did not found subdir or file and there is no next node
        }
    }

    //Assuming that the path is correct, if token_matched == 0, the file does not exist yet.
    //Thus, returns the block for the directory that will store this file
    *full_match = token_matched;
    if (*full_match == 0 && path_left_over != NULL) {
        // printf ("token left: %s\n", pch);
        strcpy(path_left_over, pch);
    }

    free(subdir_or_file_node);
    subdir_or_file_node = NULL;

    free(nodeinfo);
    free(curr_node);
    return curr_block;
}

int get_max_links_in_node(struct superblock* sb) {
    return ((sb->blksz - sizeof(struct inode)) / sizeof(uint64_t) - 1);
}

int get_num_links_in_node(struct inode* node) {
    int cnt = 0;
    while (node->links[cnt] != 0) cnt++;
    return cnt;
}

uint64_t get_last_inode(struct superblock* sb, uint64_t block) {
    struct inode* node = retrieve_inode(sb, block);

    while (node->next != 0) {
        block = node->next;
        free(node);
        node = retrieve_inode(sb, block);
    }
    free(node);
    return block;
}

uint64_t get_node_blk_with_space(struct superblock* sb, uint64_t block) {
    int max_links = get_max_links_in_node(sb);
    struct inode* node = retrieve_inode(sb, block);
    
    if (get_num_links_in_node(node) < max_links) {
        free(node);
        return block;
    }

    while (node->next != 0) {
        block = node->next;
        free(node);
        node = retrieve_inode(sb, block);
    
        if (get_num_links_in_node(node) < max_links) {
            free(node);
            return block;
        }
    }
    free(node);
    return 0;
}

void link_node_to_nodelist(struct superblock* sb, uint64_t ref_blk, uint64_t blk_to_link, uint64_t nbytes) {
    struct inode* ref_node = retrieve_inode(sb, ref_blk);
    
    if (ref_node->mode == IMCHILD) {
        //get header node
        ref_blk = ref_node->parent;
        free(ref_node);
        ref_node = retrieve_inode(sb, ref_blk);
    }

    uint64_t blk_with_space = get_node_blk_with_space(sb, ref_blk);

    if (blk_with_space != 0) {
        struct inode* node_with_space = retrieve_inode(sb, blk_with_space);
        uint64_t num_links = get_num_links_in_node(node_with_space);

        
        struct nodeinfo* tempmeta = retrieve_nodeinfo(sb, node_with_space->meta);
        free(tempmeta);

        node_with_space->links[num_links] = blk_to_link;
        node_with_space->links[num_links + 1] = 0;
        save_inode(sb, node_with_space, blk_with_space);
        free(node_with_space);
    } else {
        uint64_t new_node_block = fs_get_block(sb);
        uint64_t prev_node_block = get_last_inode(sb, ref_blk);

        struct inode* prev_node = retrieve_inode(sb, prev_node_block);
        prev_node->next = new_node_block;
        save_inode(sb, prev_node, prev_node_block);
        free(prev_node);

        struct inode* new_node = (struct inode*) calloc(1, sb->blksz);
        new_node->mode = IMCHILD;
        new_node->parent = ref_blk;
        new_node->next = 0;
        new_node->meta = prev_node_block;
        new_node->links[0] = blk_to_link;
        new_node->links[1] = 0;
        save_inode(sb, new_node, new_node_block);
        free(new_node);
    }
    struct nodeinfo* metadata = retrieve_nodeinfo(sb, ref_node->meta);
    metadata->size += (ref_node->mode == IMDIR) ? 1 : nbytes;
    save_nodeinfo(sb, metadata, ref_node->meta);

    free(ref_node);
    free(metadata);
}

void free_file_data_blocks(struct superblock* sb, uint64_t file_block) {
    struct inode* file_node = retrieve_inode(sb, file_block);
    struct nodeinfo* file_info = retrieve_nodeinfo(sb, file_node->meta);

    for(int link_index = 0; file_node->links[link_index] != 0; link_index++) {
        fs_put_block(sb, file_node->links[link_index]);
    }
    uint64_t next_blk = file_node->next;

    while (next_blk != 0) {
        struct inode* node = retrieve_inode(sb, next_blk);
        for(int link_index = 0; node->links[link_index] != 0; link_index++) {
            fs_put_block(sb, node->links[link_index]);
        }
        uint64_t curr_blk = next_blk;
        next_blk = node->next;
        fs_put_block(sb, curr_blk);
        free(node);
    }
    file_node->next = 0;
    save_inode(sb, file_node, file_block);

    file_info->size = 0;
    save_nodeinfo(sb, file_info, file_node->meta);
    
    free(file_node);
    free(file_info);
}

uint64_t create_entity(struct superblock* sb, uint64_t parent_blk, const char* ename, uint64_t mode) {
    uint64_t info_blk = fs_get_block(sb);
    struct nodeinfo* info = (struct nodeinfo*) calloc(1, sb->blksz);
    info->size = 0;
    strcpy(info->name, ename);
    save_nodeinfo(sb, info, info_blk);
    
    uint64_t e_blk = fs_get_block(sb);
    struct inode *e_node = (struct inode*) calloc(1, sb->blksz);
    e_node->mode = mode;
    e_node->parent = parent_blk;
    e_node->meta = info_blk;
    e_node->next = 0;
    e_node->links[0] = 0;
    save_inode(sb, e_node, e_blk);

    link_node_to_nodelist(sb, parent_blk, e_blk, 0); 

    free(e_node);
    free(info);
    return e_blk;
}

void write_to_file(struct superblock *sb, uint64_t file_blk, char *buf, size_t buf_sz) { 
    size_t cnt = 0;
    while (cnt + sb->blksz < buf_sz) {
        uint64_t blk = fs_get_block(sb);
        lseek(sb->fd, blk * sb->blksz, SEEK_SET);
	    write(sb->fd, buf, sb->blksz);
        link_node_to_nodelist(sb, file_blk, blk, sb->blksz);

        cnt += sb->blksz;
        buf += sb->blksz;
    }

    //remaining data
    size_t left_over = buf_sz - cnt;
    if (left_over != 0) {
        uint64_t blk = fs_get_block(sb);
        lseek(sb->fd, blk * sb->blksz, SEEK_SET);
	    write(sb->fd, buf, left_over);
        link_node_to_nodelist(sb, file_blk, blk, left_over);
    }
}

void unlink_node(struct superblock* sb, uint64_t dir_blk, uint64_t blk_to_unlink) {
    struct inode *curr_node = (struct inode*) calloc(1, sb->blksz);
    uint64_t curr_blk = dir_blk;
    int entity_index = -1;

find_inode_to_unlink:    
    lseek(sb->fd, curr_blk * sb->blksz, SEEK_SET);
    read(sb->fd, curr_node, sb->blksz);
    
    for (int index = 0; curr_node->links[index] != 0; index++) {
        if (blk_to_unlink == curr_node->links[index]) {
            entity_index = index;
            break;
        }
    }

    //go to next inode in list
    if (entity_index < 0 && curr_node->next != 0) {
        curr_blk = curr_node->next;
        goto find_inode_to_unlink; 
    }
    
    assert (entity_index >= 0);

    //shift the array tail, so empty spaces are always in the end
    for (int ii = entity_index; curr_node->links[ii] != 0; ii++) {
        curr_node->links[ii] = curr_node->links[ii + 1];
    }
    save_inode(sb, curr_node, curr_blk);
    
    if (curr_node->links[0] == 0 && curr_node->mode == IMCHILD) {
        //this node does not have to exist anymore
        struct inode* prev_node = retrieve_inode(sb, curr_node->meta);
        prev_node->next = 0;
        save_inode(sb, prev_node, curr_node->meta);
        
        fs_put_block(sb, prev_node->next);
        free(prev_node);
    }
    
    struct inode* dir_header_node = retrieve_inode(sb, dir_blk); 
    struct nodeinfo* dir_info = retrieve_nodeinfo(sb, dir_header_node->meta);
    dir_info->size--;
    save_nodeinfo(sb, dir_info, dir_header_node->meta);

    free(curr_node);
    free(dir_header_node);
    free(dir_info);
}

uint64_t get_file_size(struct superblock *sb, const char *fname) {
    int full_match = 0;
    uint64_t file_blk = get_inode_block(sb, fname, &full_match, NULL);
    
    if (full_match == 0) {
        //file does not exist
        errno = ENOENT;
        return -1;
    }

    struct inode* file_node = retrieve_inode(sb, file_blk);
    struct nodeinfo* file_info = retrieve_nodeinfo(sb, file_node->meta);
 
    uint64_t filesz = file_info->size;
    
    free(file_node);
    free(file_info);

    return filesz;
}

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>

#include "fs.h"

struct inode* retrieve_inode(struct superblock* sb, uint64_t block);
struct nodeinfo* retrieve_nodeinfo(struct superblock* sb, uint64_t block);
struct freepage* retrieve_freepage(struct superblock* sb, uint64_t block);

void save_superblock(struct superblock* sb);
void save_inode(struct superblock* sb, struct inode* node, uint64_t block);
void save_nodeinfo(struct superblock* sb, struct nodeinfo* ni, uint64_t block);
void save_freepage(struct superblock* sb, struct freepage* fp, uint64_t block);


/* This function returns the block number for the deepest matching token in the path
 * If the path was fully matched, it sets full_match to 1, otherwise 0 */
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
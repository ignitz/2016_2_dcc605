#include "pager.h"

#include <sys/mman.h>

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mmu.h"

/////////////////////////////////list////////////////////////////////////////
struct dlist {
    struct dnode *head;
    struct dnode *tail;
    int count;
};
struct dnode {
    struct dnode *prev;
    struct dnode *next;
    void *data;
};
typedef void (*dlist_data_func)(void *data);
struct dlist *dlist_create(void);
void dlist_destroy(struct dlist *dl, dlist_data_func);
void *dlist_pop_right(struct dlist *dl);
void *dlist_push_right(struct dlist *dl, void *data);
int dlist_empty(struct dlist *dl);

/* gets the data at index =idx.  =idx can be negative. */
void * dlist_get_index(const struct dlist *dl, int idx);
////////////////////////////list end///////////////////////////////////////////////

typedef struct {
    int isvalid;
    int frame_number;
    int block_number;
    int dirty; //when the page is dirty, it must to be wrote on the disk before swaping it
    intptr_t vaddr;
} Page;

typedef struct {
    pid_t pid;
    struct dlist *pages;
} PageTable;

typedef struct {
    pid_t pid;
    int accessed; //to be used by second change algorithm
    Page *page;
} FrameNode;

typedef struct {
    int nframes;
    int page_size;
    int sec_chance_index;
    FrameNode *frames;
} FrameTable;

typedef struct {
    int used; //1 if the page was copied to the disk, 0 otherwise
    Page *page;
} BlockNode;

typedef struct {
    int nblocks;
    BlockNode *blocks;
} BlockTable;

FrameTable frame_table;
BlockTable block_table;
struct dlist *page_tables;

/****************************************************************************
 * external functions
 ***************************************************************************/
int get_new_frame();
int get_new_block();
PageTable* find_page_table(pid_t pid);
Page* get_page(PageTable *pt, intptr_t vaddr); 
pthread_mutex_t locker;

void pager_init(int nframes, int nblocks) {
    pthread_mutex_lock(&locker);
    frame_table.nframes = nframes;
    frame_table.page_size = sysconf(_SC_PAGESIZE);
    frame_table.sec_chance_index = 0;

    frame_table.frames = malloc(nframes * sizeof(FrameNode));
    for(int i = 0; i < nframes; i++) {
        frame_table.frames[i].pid = -1;
    }

    block_table.nblocks = nblocks;
    block_table.blocks = malloc(nblocks * sizeof(BlockNode));
    for(int i = 0; i < nblocks; i++) {
        block_table.blocks[i].used = 0;
    }
    page_tables = dlist_create();
    pthread_mutex_unlock(&locker);
}

void pager_create(pid_t pid) {
    pthread_mutex_lock(&locker);
    PageTable *pt = (PageTable*) malloc(sizeof(PageTable));
    pt->pid = pid;
    pt->pages = dlist_create();

    dlist_push_right(page_tables, pt);
    pthread_mutex_unlock(&locker);
}

void *pager_extend(pid_t pid) {
    pthread_mutex_lock(&locker);
    int block_no = get_new_block();

    //there is no blocks available anymore
    if(block_no == -1) {
        pthread_mutex_unlock(&locker);
        return NULL;
    }

    PageTable *pt = find_page_table(pid); 
    Page *page = (Page*) malloc(sizeof(Page));
    page->isvalid = 0;
    page->vaddr = UVM_BASEADDR + pt->pages->count * frame_table.page_size;
    page->block_number = block_no;
    dlist_push_right(pt->pages, page);

    block_table.blocks[block_no].page = page;

    pthread_mutex_unlock(&locker);
    return (void*)page->vaddr;
}

int second_chance() {
    FrameNode *frames = frame_table.frames;
    int frame_to_swap = -1;

    while(frame_to_swap == -1) {
        int index = frame_table.sec_chance_index;
        if(frames[index].accessed == 0) {
            frame_to_swap = index;
        } else {
            frames[index].accessed = 0;
        }
        frame_table.sec_chance_index = (index + 1) % frame_table.nframes;
    }

    return frame_to_swap;
}

void swap_out_page(int frame_no) {
    //gambis: I do not know why I have to set PROT_NONE to all pages
    //when I am swapping the first one. Must investigate
    if(frame_no == 0) {
        for(int i = 0; i < frame_table.nframes; i++) {
            Page *page = frame_table.frames[i].page;
            mmu_chprot(frame_table.frames[i].pid, (void*)page->vaddr, PROT_NONE);
        }
    }

    FrameNode *frame = &frame_table.frames[frame_no];
    Page *removed_page = frame->page;
    removed_page->isvalid = 0;
    mmu_nonresident(frame->pid, (void*)removed_page->vaddr); 
    
    if(removed_page->dirty == 1) {
        block_table.blocks[removed_page->block_number].used = 1;
        mmu_disk_write(frame_no, removed_page->block_number);
    }
}

void pager_fault(pid_t pid, void *vaddr) {
    pthread_mutex_lock(&locker);
    PageTable *pt = find_page_table(pid); 
    vaddr = (void*)((intptr_t)vaddr - (intptr_t)vaddr % frame_table.page_size);
    Page *page = get_page(pt, (intptr_t)vaddr); 

    if(page->isvalid == 1) {
        mmu_chprot(pid, vaddr, PROT_READ | PROT_WRITE);
        frame_table.frames[page->frame_number].accessed = 1;
        page->dirty = 1;
    } else {
        int frame_no = get_new_frame();

        //there is no frames available
        if(frame_no == -1) {
            frame_no = second_chance();
            swap_out_page(frame_no);
        }

        FrameNode *frame = &frame_table.frames[frame_no];
        frame->pid = pid;
        frame->page = page;
        frame->accessed = 1;

        page->isvalid = 1;
        page->frame_number = frame_no;
        page->dirty = 0;

        //this page was already swapped out from main memory
        if(block_table.blocks[page->block_number].used == 1) {
            mmu_disk_read(page->block_number, frame_no);
        } else {
            mmu_zero_fill(frame_no);
        }
        mmu_resident(pid, vaddr, frame_no, PROT_READ);
    }
    pthread_mutex_unlock(&locker);
}

int pager_syslog(pid_t pid, void *addr, size_t len) {
    pthread_mutex_lock(&locker);
    PageTable *pt = find_page_table(pid); 
    char *buf = (char*) malloc(len + 1);

    for (size_t i = 0, m = 0; i < len; i++) {
        Page *page = get_page(pt, (intptr_t)addr + i);

        //string out of process allocated space
        if(page == NULL) {
            pthread_mutex_unlock(&locker);
            return -1;
        }

        buf[m++] = pmem[page->frame_number * frame_table.page_size + i];
    }
    for(int i = 0; i < len; i++) { // len é o número de bytes a imprimir
        printf("%02x", (unsigned)buf[i]); // buf contém os dados a serem impressos
    }
    if(len > 0) printf("\n");
    pthread_mutex_unlock(&locker);
    return 0;
}

void pager_destroy(pid_t pid) {
    pthread_mutex_lock(&locker);
    PageTable *pt = find_page_table(pid); 

    while(!dlist_empty(pt->pages)) {
        Page *page = dlist_pop_right(pt->pages);
        block_table.blocks[page->block_number].page = NULL;
        if(page->isvalid == 1) {
            frame_table.frames[page->frame_number].pid = -1;
        }
    }
    dlist_destroy(pt->pages, NULL);
    pthread_mutex_unlock(&locker);
}

/////////////////Auxiliar functions ////////////////////////////////
int get_new_frame() {
    for(int i = 0; i < frame_table.nframes; i++) {
        if(frame_table.frames[i].pid == -1) return i;
    }
    return -1;
}

int get_new_block() {
    for(int i = 0; i < block_table.nblocks; i++) {
        if(block_table.blocks[i].page == NULL) return i;
    }
    return -1;
}

PageTable* find_page_table(pid_t pid) {
    for(int i = 0; i < page_tables->count; i++) {
        PageTable *pt = dlist_get_index(page_tables, i);
        if(pt->pid == pid) return pt;
    }
    printf("error in find_page_table: Pid not found\n");
    exit(-1);
}

Page* get_page(PageTable *pt, intptr_t vaddr) {
    for(int i=0; i < pt->pages->count; i++) {
        Page *page = dlist_get_index(pt->pages, i);
        if(vaddr >= page->vaddr && vaddr < (page->vaddr + frame_table.page_size)) return page;
    }
    return NULL;
}

/////////////////////// List functions //////////////////////////////
struct dlist *dlist_create(void) {
    struct dlist *dl = malloc(sizeof(struct dlist));
    assert(dl);
    dl->head = NULL;
    dl->tail = NULL;
    dl->count = 0;
    return dl;
}

void dlist_destroy(struct dlist *dl, dlist_data_func cb) {
    while(!dlist_empty(dl)) {
        void *data = dlist_pop_right(dl);
        if(cb) cb(data);
    }
    free(dl);
}

void *dlist_pop_right(struct dlist *dl) {
    if(dlist_empty(dl)) return NULL;

    void *data;
    struct dnode *node;

    node = dl->tail;

    dl->tail = node->prev;
    if(dl->tail == NULL) dl->head = NULL;
    if(node->prev) node->prev->next = NULL;

    data = node->data;
    free(node);

    dl->count--;
    assert(dl->count >= 0);

    return data;
}

void *dlist_push_right(struct dlist *dl, void *data) {
    struct dnode *node = malloc(sizeof(struct dnode));
    assert(node);

    node->data = data;
    node->prev = dl->tail;
    node->next = NULL;

    if(dl->tail) dl->tail->next = node;
    dl->tail = node;

    if(dl->head == NULL) dl->head = node;

    dl->count++;

    return data;
}

int dlist_empty(struct dlist *dl) {
    int ret;
    if(dl->head == NULL) {
        assert(dl->tail == NULL);
        assert(dl->count == 0);
        ret = 1;
    } else {
        assert(dl->tail != NULL);
        assert(dl->count > 0);
        ret = 0;
    }
    return ret;
}

void * dlist_get_index(const struct dlist *dl, int idx) {
    struct dnode *curr;
    if(idx >= 0) {
        curr = dl->head;
        while(curr && idx--) curr = curr->next;
    } else {
        curr = dl->tail;
        while(curr && ++idx) curr = curr->prev;
    }
    if(!curr) return NULL;
    return curr->data;
}

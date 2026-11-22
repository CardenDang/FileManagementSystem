/*
 * Progress: 
    1. Create File: Create a new file and store its metadata.
    2. Delete File: Remove a file and release its allocated blocks.
    3. Write File: Write data into allocated disk blocks.
    4. Read File: Retrieve previously stored data.
 * Compile: gcc -Wall -Wextra -o filesystem filesystem.c
 * Run:     ./filesystem
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_BLOCKS   100   /* total blocks on the simulated disk */
#define BLOCK_SIZE   64    /* bytes per block                    */
#define MAX_FILES    32    /* max entries in the file table      */
#define MAX_NAME     32    /* max filename length (incl. '\0')   */
#define INPUT_BUF    1024

/* Simulated disk and bitmap */
static char disk[NUM_BLOCKS][BLOCK_SIZE];
static unsigned char bitmap[NUM_BLOCKS];   /* 0 = free, 1 = allocated */

/* File metadata */
typedef struct {
    int  in_use;               /* 1 if this slot holds a file       */
    char name[MAX_NAME];
    int  size;                 /* bytes of data actually written    */
    int  start_block;          /* first block (contiguous)          */
    int  num_blocks;           /* number of blocks allocated        */
} FileEntry;

static FileEntry file_table[MAX_FILES];

/* Helpers */

/* Read a line from stdin, strip the newline. Returns 0 on EOF. */
static int read_line(char *buf, size_t len)
{
    if (!fgets(buf, (int)len, stdin))
        return 0;
    buf[strcspn(buf, "\n")] = '\0';
    return 1;
}

/* Read an integer from stdin. Returns 1 on success. */
static int read_int(int *out)
{
    char buf[64];
    char *end;
    long v;

    if (!read_line(buf, sizeof buf))
        return 0;
    v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0')
        return 0;
    *out = (int)v;
    return 1;
}

static int find_file(const char *name)
{
    for (int i = 0; i < MAX_FILES; i++)
        if (file_table[i].in_use && strcmp(file_table[i].name, name) == 0)
            return i;
    return -1;
}

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_FILES; i++)
        if (!file_table[i].in_use)
            return i;
    return -1;
}

/* First-fit search for `count` contiguous free blocks using the bitmap.
 * Returns the starting block, or -1 if no large enough hole exists. */
static int find_contiguous(int count)
{
    int run = 0;
    for (int i = 0; i < NUM_BLOCKS; i++) {
        run = bitmap[i] ? 0 : run + 1;
        if (run == count)
            return i - count + 1;
    }
    return -1;
}

static void set_blocks(int start, int count, unsigned char value)
{
    for (int i = start; i < start + count; i++)
        bitmap[i] = value;
}

/* 1. Create File */
static void create_file(void)
{
    char name[MAX_NAME + 8];
    int blocks;

    printf("Enter file name (max %d chars): ", MAX_NAME - 1);
    if (!read_line(name, sizeof name))
        return;

    if (name[0] == '\0') {
        printf("Error: file name cannot be empty.\n");
        return;
    }
    if (strlen(name) >= MAX_NAME) {
        printf("Error: file name too long.\n");
        return;
    }
    if (find_file(name) != -1) {
        printf("Error: a file named '%s' already exists.\n", name);
        return;
    }

    int slot = find_free_slot();
    if (slot == -1) {
        printf("Error: file table is full (%d files max).\n", MAX_FILES);
        return;
    }

    printf("Enter number of blocks to allocate (1-%d, %d bytes each): ",
           NUM_BLOCKS, BLOCK_SIZE);
    if (!read_int(&blocks) || blocks < 1 || blocks > NUM_BLOCKS) {
        printf("Error: invalid block count.\n");
        return;
    }

    int start = find_contiguous(blocks);
    if (start == -1) {
        printf("Error: not enough contiguous free blocks for %d blocks.\n",
               blocks);
        return;
    }

    /* Allocate and store metadata */
    set_blocks(start, blocks, 1);
    file_table[slot].in_use = 1;
    strcpy(file_table[slot].name, name);
    file_table[slot].size = 0;
    file_table[slot].start_block = start;
    file_table[slot].num_blocks = blocks;

    printf("File '%s' created: blocks %d-%d (%d bytes capacity).\n",
           name, start, start + blocks - 1, blocks * BLOCK_SIZE);
}

/* 2. Delete File */
static void delete_file(void)
{
    char name[MAX_NAME + 8];

    printf("Enter file name to delete: ");
    if (!read_line(name, sizeof name))
        return;

    int idx = find_file(name);
    if (idx == -1) {
        printf("Error: file '%s' not found.\n", name);
        return;
    }

    FileEntry *f = &file_table[idx];

    /* Wipe the data, release the blocks in the bitmap, clear metadata */
    for (int i = f->start_block; i < f->start_block + f->num_blocks; i++)
        memset(disk[i], 0, BLOCK_SIZE);
    set_blocks(f->start_block, f->num_blocks, 0);

    printf("File '%s' deleted; released blocks %d-%d.\n", f->name,
           f->start_block, f->start_block + f->num_blocks - 1);
    memset(f, 0, sizeof *f);
}

/* 3. Write File */
static void write_file(void)
{
    char name[MAX_NAME + 8];
    char data[INPUT_BUF];

    printf("Enter file name to write to: ");
    if (!read_line(name, sizeof name))
        return;

    int idx = find_file(name);
    if (idx == -1) {
        printf("Error: file '%s' not found.\n", name);
        return;
    }
    FileEntry *f = &file_table[idx];
    int capacity = f->num_blocks * BLOCK_SIZE;

    printf("Enter data (capacity %d bytes; overwrites existing contents):\n> ",
           capacity);
    if (!read_line(data, sizeof data))
        return;

    int len = (int)strlen(data);
    if (len > capacity) {
        printf("Error: data is %d bytes but file only holds %d bytes.\n",
               len, capacity);
        return;
    }

    /* Clear old contents, then copy data block by block */
    for (int i = f->start_block; i < f->start_block + f->num_blocks; i++)
        memset(disk[i], 0, BLOCK_SIZE);

    int written = 0;
    for (int b = 0; written < len; b++) {
        int chunk = len - written;
        if (chunk > BLOCK_SIZE)
            chunk = BLOCK_SIZE;
        memcpy(disk[f->start_block + b], data + written, (size_t)chunk);
        written += chunk;
    }
    f->size = len;

    printf("Wrote %d bytes to '%s' (%d block(s) used).\n", len, f->name,
           (len + BLOCK_SIZE - 1) / BLOCK_SIZE);
}

/* 4. Read File */
static void read_file(void)
{
    char name[MAX_NAME + 8];

    printf("Enter file name to read: ");
    if (!read_line(name, sizeof name))
        return;

    int idx = find_file(name);
    if (idx == -1) {
        printf("Error: file '%s' not found.\n", name);
        return;
    }
    FileEntry *f = &file_table[idx];

    if (f->size == 0) {
        printf("File '%s' is empty.\n", f->name);
        return;
    }

    printf("--- Contents of '%s' (%d bytes) ---\n", f->name, f->size);
    int remaining = f->size;
    for (int b = 0; remaining > 0; b++) {
        int chunk = remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining;
        fwrite(disk[f->start_block + b], 1, (size_t)chunk, stdout);
        remaining -= chunk;
    }
    printf("\n--- End of file ---\n");
}

/* Menu */
static void print_menu(void)
{
    printf("\n===== File Management System (100-block disk) =====\n");
    printf("1. Create File\n");
    printf("2. Delete File\n");
    printf("3. Write File\n");
    printf("4. Read File\n");
    printf("0. Exit\n");
    printf("Choice: ");
}

int main(void)
{
    /* Disk starts empty: all blocks free, no files */
    memset(disk, 0, sizeof disk);
    memset(bitmap, 0, sizeof bitmap);
    memset(file_table, 0, sizeof file_table);

    int choice;
    for (;;) {
        print_menu();
        if (!read_int(&choice)) {
            if (feof(stdin))
                break;
            printf("Invalid input. Enter a number from the menu.\n");
            continue;
        }
        switch (choice) {
        case 1: create_file(); break;
        case 2: delete_file(); break;
        case 3: write_file();  break;
        case 4: read_file();   break;
        case 0: printf("Goodbye!\n"); return 0;
        default: printf("Invalid choice.\n");
        }
    }
    return 0;
}
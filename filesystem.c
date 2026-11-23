/*
 * CS-149 File Management System
 *
 * Requirements implemented:
 *   1. Create File: Create a file and store its metadata.
 *   2. Delete File: Remove a file and release its allocated blocks.
 *   3. Write File: Overwrite data in the file's allocated disk blocks.
 *   4. Read File: Retrieve and display previously stored data.
 *
 * Storage model:
 *   - A simulated disk with 100 blocks of 64 bytes each.
 *   - First-fit contiguous block allocation and a free-space bitmap.
 *   - Up to 32 files, each with a name of at most 31 bytes.
 *   - All data is kept in memory and is lost when the program exits.
 *   - Simulated file operations do not change the computer's real files.
 *
 * Compile: gcc -std=c11 -Wall -Wextra -Wpedantic -o filesystem filesystem.c
 * Run:     ./filesystem
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_BLOCKS 100  /* Number of blocks on the simulated disk */
#define BLOCK_SIZE 64   /* Number of bytes in each block          */
#define MAX_FILES 32    /* Maximum number of file table entries   */
#define MAX_NAME 32     /* Maximum filename bytes, including NUL */
#define MAX_DATA (NUM_BLOCKS * BLOCK_SIZE)

/* The simulated disk and its free-space bitmap (0 = free, 1 = allocated). */
static char disk[NUM_BLOCKS][BLOCK_SIZE];
static unsigned char bitmap[NUM_BLOCKS];

typedef struct {
    int in_use;                  /* Whether this entry describes a file */
    char name[MAX_NAME];
    int size;                    /* Number of bytes written to the file */
    int start_block;             /* First allocated block (contiguous)  */
    int num_blocks;              /* Total allocated blocks              */
} FileEntry;

static FileEntry file_table[MAX_FILES];

typedef enum {
    INPUT_INVALID = -2,
    INPUT_TOO_LONG = -1,
    INPUT_EOF = 0,
    INPUT_OK = 1
} InputStatus;

/*
 * Read a complete input line and discard the trailing newline.
 * An oversized line is fully consumed, so it cannot affect the next menu choice.
 * actual_length, when non-NULL, receives the original line length in bytes.
 */
static InputStatus read_line(char *buf, size_t buf_size, size_t *actual_length)
{
    size_t length = 0;
    int ch;

    while ((ch = getchar()) != '\n' && ch != EOF) {
        if (length < buf_size - 1)
            buf[length] = (char)ch;
        length++;
    }

    buf[length < buf_size ? length : buf_size - 1] = '\0';
    if (actual_length != NULL)
        *actual_length = length;

    if (ch == EOF && length == 0)
        return INPUT_EOF;
    return length >= buf_size ? INPUT_TOO_LONG : INPUT_OK;
}

/* Read a whole integer, allowing surrounding spaces but not extra text. */
static InputStatus read_int(int *out)
{
    char buf[64];
    char *end;
    long value;
    InputStatus status = read_line(buf, sizeof buf, NULL);

    if (status != INPUT_OK)
        return status;

    errno = 0;
    value = strtol(buf, &end, 10);
    if (errno == ERANGE || end == buf || value < INT_MIN || value > INT_MAX)
        return INPUT_INVALID;

    while (isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return INPUT_INVALID;

    *out = (int)value;
    return INPUT_OK;
}

/* Prompt for a valid filename, shared by all four file operations. */
static int prompt_filename(const char *prompt, char name[MAX_NAME])
{
    InputStatus status;
    size_t i;

    printf("%s", prompt);
    fflush(stdout);
    status = read_line(name, MAX_NAME, NULL);

    if (status == INPUT_EOF)
        return 0;
    if (status == INPUT_TOO_LONG) {
        printf("Error: filename must be at most %d bytes long.\n", MAX_NAME - 1);
        return 0;
    }

    for (i = 0; name[i] != '\0'; i++) {
        if (!isspace((unsigned char)name[i]))
            return 1;
    }
    printf("Error: filename cannot be empty or only spaces.\n");
    return 0;
}

static int find_file(const char *name)
{
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_table[i].in_use && strcmp(file_table[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_FILES; i++) {
        if (!file_table[i].in_use)
            return i;
    }
    return -1;
}

/* First-fit search for a contiguous run of count unused disk blocks. */
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

static int free_block_count(void)
{
    int free_blocks = 0;

    for (int i = 0; i < NUM_BLOCKS; i++) {
        if (!bitmap[i])
            free_blocks++;
    }
    return free_blocks;
}

/* Requirement 1: Create File. */
static void create_file(void)
{
    char name[MAX_NAME];
    int blocks;
    int slot;
    int start;
    InputStatus status;

    printf("\n--- Create File ---\n");
    if (!prompt_filename("File name: ", name))
        return;
    if (find_file(name) != -1) {
        printf("Error: '%s' already exists.\n", name);
        return;
    }

    slot = find_free_slot();
    if (slot == -1) {
        printf("Error: file table is full (maximum %d files).\n", MAX_FILES);
        return;
    }

    printf("Blocks to allocate (1-%d, %d bytes each): ", NUM_BLOCKS, BLOCK_SIZE);
    fflush(stdout);
    status = read_int(&blocks);
    if (status == INPUT_EOF)
        return;
    if (status != INPUT_OK || blocks < 1 || blocks > NUM_BLOCKS) {
        printf("Error: enter a whole number between 1 and %d.\n", NUM_BLOCKS);
        return;
    }

    start = find_contiguous(blocks);
    if (start == -1) {
        printf("Error: cannot allocate %d contiguous blocks (%d total blocks free).\n",
               blocks, free_block_count());
        return;
    }

    /* Allocate the blocks, then initialize the file's metadata. */
    set_blocks(start, blocks, 1);
    FileEntry *f = &file_table[slot];
    f->in_use = 1;
    strcpy(f->name, name);
    f->size = 0;
    f->start_block = start;
    f->num_blocks = blocks;

    printf("Created '%s': blocks %d-%d, capacity %d bytes.\n",
           f->name, start, start + blocks - 1, blocks * BLOCK_SIZE);
}

/* Requirement 2: Delete File. */
static void delete_file(void)
{
    char name[MAX_NAME];

    printf("\n--- Delete File ---\n");
    if (!prompt_filename("File name: ", name))
        return;

    int idx = find_file(name);
    if (idx == -1) {
        printf("Error: '%s' was not found.\n", name);
        return;
    }

    FileEntry *f = &file_table[idx];
    int start = f->start_block;
    int blocks = f->num_blocks;

    /* Erase data, release the blocks, and remove the file table entry. */
    for (int i = start; i < start + blocks; i++)
        memset(disk[i], 0, BLOCK_SIZE);
    set_blocks(start, blocks, 0);
    memset(f, 0, sizeof *f);

    printf("Deleted '%s' and released blocks %d-%d (%d blocks).\n",
           name, start, start + blocks - 1, blocks);
}

/* Requirement 3: Write File. */
static void write_file(void)
{
    char name[MAX_NAME];
    char data[MAX_DATA + 1];
    size_t input_length;
    InputStatus status;

    printf("\n--- Write File ---\n");
    if (!prompt_filename("File name: ", name))
        return;

    int idx = find_file(name);
    if (idx == -1) {
        printf("Error: '%s' was not found. Create it first.\n", name);
        return;
    }

    FileEntry *f = &file_table[idx];
    int capacity = f->num_blocks * BLOCK_SIZE;

    printf("Enter one line of data (capacity %d bytes; replaces existing data):\n> ",
           capacity);
    fflush(stdout);
    status = read_line(data, sizeof data, &input_length);

    if (status == INPUT_EOF)
        return;
    if (status == INPUT_TOO_LONG) {
        printf("Error: input is %zu bytes; the simulated disk can hold at most %d bytes.\n",
               input_length, MAX_DATA);
        return;
    }
    if (input_length > (size_t)capacity) {
        printf("Error: input is %zu bytes, but '%s' has only %d bytes of capacity.\n",
               input_length, f->name, capacity);
        return;
    }

    /* Only change stored data after validating the entire input. */
    for (int i = f->start_block; i < f->start_block + f->num_blocks; i++)
        memset(disk[i], 0, BLOCK_SIZE);

    size_t written = 0;
    for (int b = 0; written < input_length; b++) {
        size_t chunk = input_length - written;
        if (chunk > BLOCK_SIZE)
            chunk = BLOCK_SIZE;
        memcpy(disk[f->start_block + b], data + written, chunk);
        written += chunk;
    }

    f->size = (int)input_length;
    printf("Wrote %d bytes to '%s' (%d of %d allocated blocks contain data).\n",
           f->size, f->name, (f->size + BLOCK_SIZE - 1) / BLOCK_SIZE,
           f->num_blocks);
}

/* Requirement 4: Read File. */
static void read_file(void)
{
    char name[MAX_NAME];

    printf("\n--- Read File ---\n");
    if (!prompt_filename("File name: ", name))
        return;

    int idx = find_file(name);
    if (idx == -1) {
        printf("Error: '%s' was not found.\n", name);
        return;
    }

    FileEntry *f = &file_table[idx];
    if (f->size == 0) {
        printf("'%s' is empty (0 bytes).\n", f->name);
        return;
    }

    printf("\n--- Contents of '%s' (%d bytes) ---\n", f->name, f->size);
    int remaining = f->size;
    for (int b = 0; remaining > 0; b++) {
        int chunk = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        fwrite(disk[f->start_block + b], 1, (size_t)chunk, stdout);
        remaining -= chunk;
    }
    printf("\n--- End of file ---\n");
}

static void print_menu(void)
{
    printf("\n========================================\n");
    printf("       FILE MANAGEMENT SYSTEM\n");
    printf("========================================\n");
    printf("Simulated disk: %d blocks x %d bytes\n", NUM_BLOCKS, BLOCK_SIZE);
    printf("Free blocks: %d / %d\n", free_block_count(), NUM_BLOCKS);
    printf("----------------------------------------\n");
    printf("  1. Create File\n");
    printf("  2. Delete File\n");
    printf("  3. Write File\n");
    printf("  4. Read File\n");
    printf("  0. Exit\n");
    printf("----------------------------------------\n");
    printf("Select an option: ");
    fflush(stdout);
}

int main(void)
{
    int choice;

    /* Begin each run with an empty simulated disk and file table. */
    memset(disk, 0, sizeof disk);
    memset(bitmap, 0, sizeof bitmap);
    memset(file_table, 0, sizeof file_table);

    for (;;) {
        print_menu();
        InputStatus status = read_int(&choice);
        if (status == INPUT_EOF) {
            printf("\nGoodbye!\n");
            break;
        }
        if (status != INPUT_OK) {
            printf("Invalid input. Please enter a menu number from 0 to 4.\n");
            continue;
        }

        switch (choice) {
        case 1: create_file(); break;
        case 2: delete_file(); break;
        case 3: write_file();  break;
        case 4: read_file();   break;
        case 0: printf("Goodbye!\n"); return 0;
        default: printf("Invalid choice. Select 0, 1, 2, 3, or 4.\n");
        }
    }
    return 0;
}

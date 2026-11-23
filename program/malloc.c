#include <stdio.h>
#include <stdlib.h>  
#include <malloc.h>
#include <sys.h>

int version = 1;

#define SWAP_TEST_CHILDREN 6
#define SWAP_TEST_PAGES 170
#define WORDS_PER_PAGE 1024

int verify_swap_pages()
{
    int pid = getpid();
    unsigned int base = brk(0);
    int page, offset;
    volatile unsigned int *words;
    unsigned int expected;
    if (brk((void *)(base + SWAP_TEST_PAGES * 4096)) < 0)
    {
        printf("SWAPTEST PID %d allocation failed\n", pid);
        return 2;
    }

    for (page = 0; page < SWAP_TEST_PAGES; page++)
    {
        words = (volatile unsigned int *)(base + page * 4096);
        for (offset = 0; offset < WORDS_PER_PAGE; offset++)
            words[offset] = 0x5A000000U ^ (pid << 16) ^ (page << 10) ^ offset;
    }
    printf("SWAPTEST PID %d wrote %d pages\n", pid, SWAP_TEST_PAGES);

    sleep(5);
    for (page = 0; page < SWAP_TEST_PAGES; page++)
    {
        words = (volatile unsigned int *)(base + page * 4096);
        for (offset = 0; offset < WORDS_PER_PAGE; offset++)
        {
            expected = 0x5A000000U ^ (pid << 16) ^ (page << 10) ^ offset;
            if (words[offset] != expected)
            {
                printf("SWAPTEST PID %d mismatch page %d word %d\n", pid, page, offset);
                return 1;
            }
        }
    }
    printf("SWAPTEST PID %d readback OK\n", pid);
    return 0;
}

int main1(int argc, char *argv[])
{  
    int i, j;
    if (argc > 1 && argv[1][0] == 's' && argv[1][1] == 'w' && argv[1][2] == 'a' &&
        argv[1][3] == 'p' && argv[1][4] == '\0')
    {
        int started = 0;
        int passed = 0;
        int child, pid, status;
        for (child = 0; child < SWAP_TEST_CHILDREN; child++)
        {
            pid = fork();
            if (pid == 0)
                exit(verify_swap_pages());
            if (pid < 0)
                break;
            started++;
        }
        for (child = 0; child < started; child++)
        {
            status = -1;
            if (wait(&status) > 0 && status == 0)
                passed++;
        }
        printf("SWAPTEST %d/%d children passed\n", passed, started);
        return started == SWAP_TEST_CHILDREN && passed == started ? 0 : 1;
    }

    printf("The address of version is %x\n", &version);

    printf("start address of initial heap = %x\n", sbrk(0));
    int *c = malloc(128);
    printf("the address of c: %x\n", c);
    int *d = malloc(256);
    printf("the address of d: %x\n", d);
    int *e = malloc(256);
    printf("the address of e: %x\n", e);
    free(d); 
    int *f = malloc(83);
    printf("the address of f: %x\n", f);

    printf("the end of heap: %x\n", sbrk(0));

    f = malloc(3*4096);
    printf("the end of heap(after another expand): %x\n", sbrk(0));

    return 0; 
}

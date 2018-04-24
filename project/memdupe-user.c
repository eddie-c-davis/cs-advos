#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "memdupe.h"

static int cpl_check(void) {
    uint csr, mask, cpl;
    asm("movl %%cs,%0" : "=r" (csr));

    mask = (1 << 2) - 1;
    cpl = csr & mask;

    if (cpl != CPL_USER) {
        printf("<memdupe> Error: not running in user mode (%d)\n", cpl);
    }

    return cpl;
}

static int virt_test(void) {
    char line[BUFFER_SIZE];
    uint virt_on = FALSE;
    FILE *fp;

    /* Check /proc/cpuinfo file for 'hypervisor' flag */
    fp = fopen("/proc/cpuinfo", "r");
    if (fp != NULL) {
        while (!virt_on && fgets(line, BUFFER_SIZE, fp) != NULL) {
            if (strstr(line, "hypervisor") != NULL) {
                virt_on = TRUE;
            }
        }

        fclose(fp);
    } else {
        printf("<memdupe> Error: Could not open file '/proc/cpuinfo'\n");
    }

    return virt_on;
}

static ulong get_clock_time(void) {
    struct timespec ts;
    ulong now;

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    now = ts.tv_sec * BILLION + ts.tv_nsec;

    return now;
}

static char *load_file(const char *path, ulong *fsize) {
    char *data;
    FILE *fp;

    // Open file
    fp = fopen(path, "rb");

    if (fp != NULL) {
        /* Get file size */
        fseek(fp, 0, SEEK_END);
        *fsize = ftell(fp);
        rewind(fp);

        // Allocate buffer...
        data = (char *) malloc(*fsize + 1);

        if (data != NULL) {
            printf("<memdupe> Reading file: '%s'\n", path);
            fread(data, *fsize, 1, fp);
            data[*fsize] = '\0';  // Terminate string
        } else {
            printf("<memdupe> Error allocating data: %ld bytes\n", *fsize);
            *fsize = 0;
        }

        // Close file
        fclose(fp);
    } else {
        printf("<memdupe> Error opening file: '%s'\n", path);
        *fsize = 0;
    }

    return data;
}

static ulong write_pages(char** data, ulong pages, char *msg) {
    ulong index = 0;
    ulong time1 = 0;
    ulong time2 = 0;

    char *buffer;

    buffer = (char *) malloc(sizeof(char) * pages);
    memset(buffer, '.', sizeof(char) * pages);

    if (strlen(msg) > 0) {
        strcpy(buffer, msg);
    }

    /* Start timer for writing pages... */
    time1 = get_clock_time();

    do {
        (*data)[pages * MY_PAGE_SIZE - 1] = buffer[index];
        index++;
        pages--;
    } while (pages > 0);

    /* Stop timer */
    time2 = get_clock_time();

    free(buffer);

    return (time2 - time1);
}

static char *read_pages(char** data, ulong pages) {
    char *buffer = NULL;
    ulong index = 0;

    buffer = (char *) malloc(sizeof(char) * pages);
    memset(buffer, '.', sizeof(char) * pages);

    do {
        buffer[index] = (*data)[pages * MY_PAGE_SIZE - 1];
        index++;
        pages--;
    } while (pages > 0);

    return buffer;
}

static char *encode_message(char *msg, uint *nbits) {
    char byte;
    char buff[BYTEBITS];
    char *bits;
    int i, j;
    uint index;
    uint nchars;

    nchars = strlen(msg);
    *nbits = nchars * BYTEBITS;

    bits = (char*) malloc(*nbits);

    index = 0;
    for (i = 0; i < nchars; i++) {
        for (j = 0; j < BYTEBITS; j++) {
            buff[j] = (msg[i] & (BITMASK << j)) != 0;
        }

        for (j = BYTEBITS - 1; j >= 0; j--) {
            bits[index++] = buff[j];
        }
    }

    printf("encode_message: ");
    for (i = 0; i < *nbits; i++) {
        printf("%d", bits[i]);
        if (i % 8 == 7) {
            printf(" ");
        }
    }
    printf("\n");

    return bits;
}

static char *decode_message(char *bits, uint nbits) {
    char bit;
    char val = '\0';
    char *msg = NULL;
    int i, j;
    uint index;
    uint nchars;

    nchars = nbits / BYTEBITS;
    msg = (char *) malloc(nchars + 1);

    index = 0;
    for (i = 0; i < nbits; i += BYTEBITS) {
        val = 0;
        for (j = 0; j < BYTEBITS; j++) {
            bit = bits[i + j];
            val *= 2;
            val += bit;
        }
        msg[index] = val;
        index += 1;
    }

    msg[index] = '\0';

    printf("decode_message: %s\n", msg);

    return msg;
}

static void free_data(char** data0, char **data1, char **data2) {
    free(*data0);
    free(*data1);
    free(*data2);
}

static int memdupe_init(void) {
    char *data0, *data1, *data2;
    char *msg;

    uint vmx_on = FALSE;
    uint vm_stat = 0;
    uint cpl_flag = 0;
    uint nbits;

    ulong fsize;
    ulong pages;
    ulong wtime = 0;
    ulong w2time = 0;

    float ratio = 0.0;

    char *bits = encode_message(MESSAGE, &nbits);
    msg = decode_message(bits, nbits);
    free(bits);
    free(msg);

    /* Test virtualization */
    vmx_on = virt_test();
    if (vmx_on) {
        printf("<memdupe> Running memdupe_init in guest mode\n");
    } else {
        printf("<memdupe> Running memdupe_init in host mode\n");
    }

    /* Get CPL flag */
    cpl_flag = cpl_check();

    if (cpl_flag == CPL_USER) {
        /* Load a file */
        data0 = load_file(FILEPATH, &fsize);

        if (fsize > 0 && data0 != NULL) {
            pages = fsize / MY_PAGE_SIZE;
            printf("<memdupe> Read file of size %ld B, %ld pages\n", fsize, pages);

            /* Write pages once... */
            wtime = write_pages(&data0, pages, "");
            printf("<memdupe> Wrote %ld pages once in %ld ns\n", pages, wtime);

            /* Load file 2 more times */
            data1 = load_file(FILEPATH, &fsize);
            data2 = load_file(FILEPATH, &fsize);
            printf("<memdupe> Read file '%s' 2 more times\n", FILEPATH);

            fprintf(stderr, "pre-sleep: data0 = %p, data1 = %p, data2 = %p\n", data0, data1, data2);

            /* Sleep... */
            sleep(_sleeptime);
            printf("<memdupe> Slept for %d seconds\n", _sleeptime);

            fprintf(stderr, "post-sleep: data0 = %p, data1 = %p, data2 = %p\n", data0, data1, data2);

            /* Write pages again... */
            w2time = write_pages(&data0, pages, MESSAGE);
            printf("<memdupe> Wrote %ld pages again in %ld ns\n", pages, w2time);

            fprintf(stderr, "post-write: data0 = %p, data1 = %p, data2 = %p\n", data0, data1, data2);

            ratio = (float) w2time / (float) wtime;
            vm_stat = (ratio > (float) KSM_THRESHOLD) ? TRUE : FALSE;

            printf("<memdupe> Ratio = %g = %ld / %ld, Threshold = %d, VM_Status = %d\n",
                   ratio, w2time, wtime, KSM_THRESHOLD, vm_stat);

            if (vm_stat) {
                if (_vmrole == RECEIVER) {
                    /* KSM tells us we are running on a VM, create channel to other VM... */
                    msg = read_pages(&data0, pages);
                    printf("<memdupe> Read message '%s' from covert channel\n", msg);
                    free(msg);
                } else {
                    printf("<memdupe> Wrote message '%s' into covert channel\n", MESSAGE);
                }
            } else {
                printf("<memdupe> Memory deduplication did not occur\n");
            }

            // Avoid memory leaks...
            free_data(&data0, &data1, &data2);
            printf("<memdupe> Freed data pointers\n");
        }
    }

    return vm_stat;
}

static void memdupe_exit(void) {
    printf("<memdupe> Done\n");
}

int main(int argc, char **argv) {
    uint status;

    if (argc > 2) {
        _sleeptime = atoi(argv[2]);
    } else {
        _sleeptime = NUM_SECONDS;
    }

    if (argc > 1) {
        _vmrole = atoi(argv[1]);
    } else {
        _vmrole = 0;
    }

    status = memdupe_init();
    memdupe_exit();

    return status;
}

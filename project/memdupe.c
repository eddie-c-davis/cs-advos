#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

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
    int fd;
    struct stat st;

    // Open file
    fd = open(path, O_RDONLY);

    if (fd >= 0) {
        /* Get file size */
        fstat(fd, &st);
        *fsize = st.st_size;

        // Allocate buffer...
        printf("<memdupe> Reading file: '%s'\n", path);
        data = (char*) mmap(NULL, *fsize + 1, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);

        if (data != NULL) {
            read(fd, data, *fsize);
            data[*fsize] = '\0';  // Terminate string
            madvise(data, *fsize + 1, MADV_MERGEABLE);
        } else {
            printf("<memdupe> Error allocating data: %ld bytes\n", *fsize);
            *fsize = 0;
        }

        // Close file
        close(fd);
    } else {
        printf("<memdupe> Error opening file: '%s'\n", path);
        *fsize = 0;
    }

    return data;
}

static ulong write_pages(char** data, ulong pages, uint step) {
    char byte;
    char *bits = NULL;
    uint dowrite = 0;
    uint islong = 0;
    ulong nbits = 0;
    ulong index = 0;
    ulong tinit = 0;
    ulong time1 = 0;
    ulong time2 = 0;
    ulong tend = 0;
    ulong tdiff = 0;
    ulong tsum = 0;
    ulong tmean = 0;

    if (step == 1 && _vmrole == SENDER) {
        bits = encode_message(MESSAGE, &nbits);
        if (nbits > pages) {
            nbits = pages;
        }
    } else {
        /* Write every page if not the SENDER */
        nbits = pages;
        bits = (char *) malloc(nbits);
        memset(bits, 1, nbits);
    }

    /* Start timer for writing pages... */
    tinit = get_clock_time();

    if (step == 1 && _vmrole > TESTER) {
        fprintf(stderr, "Op,Page,Time,Long?\n");
    }

    do {
        if (_vmrole > TESTER) {
            time1 = get_clock_time();
        }

        /* Write to bits that differ */
        dowrite = (nbits > index && bits[index]);
        if (dowrite) {
            (*data)[pages * MY_PAGE_SIZE - 1] = '.';
        }

        if (_vmrole > TESTER) {
            time2 = get_clock_time();
            tdiff = time2 - time1;
            tsum += tdiff;
            tmean = tsum / (index + 1);
            islong = (tdiff > KSM_THRESHOLD * tmean);

            if (dowrite && step == 1 && _vmrole == SENDER) {
                fprintf(stderr, "W,%ld,%ld,%d\n", index, tdiff, islong);
            } else if (step > 1 && _vmrole == RECEIVER) {
                fprintf(stderr, "R,%ld,%ld,%d\n", index, tdiff, islong);
            }
        }

        index++;
        pages--;
    } while (pages > 0);

    /* Stop timer */
    tend = get_clock_time();
    tdiff = tend - tinit;

    if (nbits > 0) {
        free(bits);
    }

    return tdiff;
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

static char *encode_message(char *msg, ulong *nbits) {
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

static char *decode_message(char *bits, ulong nbits) {
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

static void free_data(ulong fsize, char** data0, char **data1, char **data2) {
    munmap(data0, fsize + 1);
    munmap(data1, fsize + 1);
    munmap(data2, fsize + 1);
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
        /* 1) Load a file (same data into memory) -- Sender / Receiver */
        data0 = load_file(_filepath, &fsize);

        if (fsize > 0 && data0 != NULL) {
            pages = fsize / MY_PAGE_SIZE;
            printf("<memdupe> Read file of size %ld B, %ld pages\n", fsize, pages);

            /* 2) Write pages once... -- Sender encodes message */
            wtime = write_pages(&data0, pages, 1);
            printf("<memdupe> Wrote %ld pages once in %ld ns\n", pages, wtime);

            /* Load file 2 more times */
            data1 = load_file(_filepath, &fsize);
            data2 = load_file(_filepath, &fsize);
            printf("<memdupe> Read file '%s' 2 more times\n", _filepath);

            //fprintf(stderr, "pre-sleep: data0 = %p, data1 = %p, data2 = %p\n", data0, data1, data2);

            /* 3) Sleep and wait for KSM to work -- Sender / Receiver*/
            sleep(_sleeptime);
            printf("<memdupe> Slept for %d seconds\n", _sleeptime);

            //fprintf(stderr, "post-sleep: data0 = %p, data1 = %p, data2 = %p\n", data0, data1, data2);

            /* 4) Write pages again and detect the ones that take longer to write -- Receiver... */
            w2time = write_pages(&data0, pages, 2);
            printf("<memdupe> Wrote %ld pages again in %ld ns\n", pages, w2time);

            //fprintf(stderr, "post-write: data0 = %p, data1 = %p, data2 = %p\n", data0, data1, data2);

            ratio = (float) w2time / (float) wtime;
            vm_stat = (ratio > (float) KSM_THRESHOLD) ? TRUE : FALSE;

            printf("<memdupe> Ratio = %g = %ld / %ld, Threshold = %d, VM_Status = %d\n",
                   ratio, w2time, wtime, KSM_THRESHOLD, vm_stat);

            if (_vmrole == TESTER) {
                if (vm_stat) {
                    printf("<memdupe> Memory deduplication probably occured\n");
                } else {
                    printf("<memdupe> Memory deduplication did not occur\n");
                }
            }

            // Avoid memory leaks...
            free_data(fsize, &data0, &data1, &data2);
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

    if (argc > 3) {
        strcpy(_filepath, argv[3]);
    } else {
        strcpy(_filepath, FILEPATH);
    }

    if (argc > 2) {
        _sleeptime = atoi(argv[2]);
    } else {
        _sleeptime = NUM_SECONDS;
    }

    if (argc > 1) {
        if (strstr(argv[1], "-h")) {
            printf("usage: memdupe ROLE[0=TESTER|1=SENDER|2=RECEIVER] SLEEPTIME=5 FILEPATH=/usr/bin/perl\n");
            _vmrole = -1;
        } else {
            _vmrole = atoi(argv[1]);
        }
    } else {
        _vmrole = 0;
    }

    if (_vmrole >= 0) {
        // Check this file:
        // /sys/kernel/mm/ksm/pages_shared
        status = memdupe_init();
        memdupe_exit();
    }

    return status;
}

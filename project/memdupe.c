/**
 * @author Eddie Davis
 * @project memdupe
 * @file memdupe.c
 * @headerfile memdupe.h
 * @brief Detect memory duplication using KSM in KVM.
 * @date 4-25-2018
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "memdupe.h"

/**
 * cpl_check
 * Check the CPL register to get privilege level (0=kernel, 3=user)
 * @return CPU privilege level
 */
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

/**
 * virt_test
 * @brief Parses /proc/cpuinfo and checks for the hypervisor flag.
 * @return True if running in guest mode
 */
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

/**
 * get_clock_time
 * @brief Get CPU clock time in nanoseconds using clock_gettime.
 * @return CPU clock time in nanoseconds
 */
static ulong get_clock_time(void) {
    struct timespec ts;
    ulong now = 0;

    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) >= 0) {
        now = ts.tv_sec * BILLION + ts.tv_nsec;
    }

    return now;
}

/**
 * load_file
 * @brief Load file into memory
 * @param path Path of file to load
 * @param fsize Pointer to file size
 * @return Pointer to the buffer containing the file
 */
static char *load_file(const char *path, ulong *fsize) {
    char *data;
    int fd;
    struct stat st;

    // Open the file
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        /* Get file size */
        fstat(fd, &st);
        *fsize = st.st_size;

        // Allocate buffer using mmap
        printf("<memdupe> Reading file: '%s'\n", path);
        data = (char*) mmap(NULL, *fsize + 1, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);

        if (data != NULL) {
            // Read the file
            read(fd, data, *fsize);
            data[*fsize] = '\0';  // Terminate string
            // Indicate that the buffer can be merged by KSM
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

/**
 * write_pages
 * @param data Pointer to the file data in memory
 * @param pages Number of pages occupied by the file
 * @param step Step indicates whether first write or second
 * @return Clock time required to write pages (ns)
 */
static ulong write_pages(char** data, ulong pages, uint step) {
    char byte;
    char *bits = NULL;
    char *msg = NULL;
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

    if (_vmrole == SENDER) {
        /* Encode the message bytes => bits if the Sender */
        bits = encode_message(_message, &nbits);
        if (nbits > pages) {
            nbits = pages;
        }
    } else {
        /* Write every page if not the Sender */
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

        /* Write to 1 bits */
        dowrite = (nbits > index && bits[index]);
        if (dowrite) {
            (*data)[pages * MY_PAGE_SIZE - 1] = '.';
        }

        if (_vmrole > TESTER) {
            // Calculate time to write page
            time2 = get_clock_time();
            tdiff = time2 - time1;

            // Calculate the running mean and determine if it exceeds the KSM threshold
            tsum += tdiff;
            tmean = tsum / (index + 1);
            islong = (tdiff > _ksmthresh * tmean);

            if (dowrite && _vmrole == SENDER) {
                fprintf(stderr, "W,%ld,%ld,%d\n", index, tdiff, islong);
            } else if (step > 1 && _vmrole == RECEIVER) {
                fprintf(stderr, "R,%ld,%ld,%d\n", index, tdiff, islong);
                // If write time is long, COW means page has been deduplicated by receier
                bits[index] = !islong;
            }
        }

        index++;
        pages--;
    } while (pages > 0);

    /* Stop timer */
    tend = get_clock_time();
    tdiff = tend - tinit;

    /* Decode the message if Receiver */
    if (step > 1 && _vmrole == RECEIVER) {
        msg = decode_message(bits, index);
        free(msg);
    }

    // Free memory...
    if (nbits > 0) {
        free(bits);
    }

    return tdiff;
}

/**
 * encode_message
 * @brief Encode message before sending through covert channel (bytes => bits)
 * @param msg The message to be encoded (bytes)
 * @param nbits Pointer to the number of bits in the encoded message
 * @return Pointer to the encoded buffer (bits)
 */
static char *encode_message(char *msg, ulong *nbits) {
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

    printf("<memdupe> Encoded message: '%s' => ", msg);
    for (i = 0; i < *nbits; i++) {
        printf("%d", bits[i]);
        if (i % 8 == 7) {
            printf(" ");
        }
    }
    printf("\n");

    return bits;
}

/**
 * decode_message
 * @brief Decode message received through covert channel (bits => bytes)
 * @param bits Pointer to array of bits from writing pages
 * @param nbits Number of bits in the message
 * @return Pointer to the decoded message
 */
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
            bit = bits[i + j] ? 1 : 0;
            val *= 2;
            val += bit;
        }
        msg[index] = val;
        index += 1;
    }

    msg[index] = '\0';
    printf("<memdupe> Decoded message: '%s'\n", msg);

    return msg;
}

/**
 * free_data
 * @brief Free data allocated during execution
 * @param fsize File size loaded in each file
 * @param data0 First data pointer
 * @param data1 Second data pointer
 * @param data2 Third data pointer
 */
static void free_data(ulong fsize, char** data0, char **data1, char **data2) {
    munmap(data0, fsize + 1);
    if (data1 != NULL) {
        munmap(data1, fsize + 1);
    }
    if (data2 != NULL) {
        munmap(data2, fsize + 1);
    }
}

/**
 * memdupe_init
 * @brief Setup and run
 * @return Virtualization status
 */
static int memdupe_init(void) {
    char *data0, *data1 = NULL, *data2 = NULL;

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

            /* Load file 2 more times */
            if (_readtwice) {
                data1 = load_file(_filepath, &fsize);
                data2 = load_file(_filepath, &fsize);
                printf("<memdupe> Read file '%s' 2 more times\n", _filepath);
            }

            /* 2) Write pages once... -- Sender encodes message */
            wtime = write_pages(&data0, pages, 1);
            printf("<memdupe> Wrote %ld pages once in %ld ns\n", pages, wtime);

            /* 3) Sleep and wait for KSM to work -- Sender / Receiver*/
            printf("<memdupe> Sleep for %d seconds\n", _sleeptime);
            sleep(_sleeptime);

            /* 4) Write pages again and detect the ones that take longer to write -- Receiver... */
            if (_vmrole != SENDER) {
                w2time = write_pages(&data0, pages, 2);
                printf("<memdupe> Wrote %ld pages again in %ld ns\n", pages, w2time);

                ratio = (float) w2time / (float) wtime;
                vm_stat = (ratio > (float) _ksmthresh) ? TRUE : FALSE;

                printf("<memdupe> Ratio = %g = %ld / %ld, Threshold = %d, VM_Status = %d\n",
                       ratio, w2time, wtime, _ksmthresh, vm_stat);
            }

            if (_vmrole == TESTER) {
                if (vm_stat) {
                    printf("<memdupe> Memory deduplication probably occurred\n");
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

/**
 * memdupe_exit
 * @brief Finish and perform any cleanup
 */
static void memdupe_exit(void) {
    printf("<memdupe> Done\n");
}

/**
 * Main function
 * @param argc Arg count
 * @param argv Arguments
 * @return Exit status
 */
int main(int argc, char **argv) {
    uint status;

    if (argc > 6) {
        _readtwice = atoi(argv[6]);
    } else {
        _readtwice = TRUE;
    }

    if (argc > 5) {
        strcpy(_message, argv[5]);
    } else {
        strcpy(_message, MESSAGE);
    }

    if (argc > 4) {
        _ksmthresh = atoi(argv[4]);
    } else {
        _ksmthresh = KSM_THRESHOLD;
    }

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
            printf("usage: memdupe ROLE[0=TESTER|1=SENDER|2=RECEIVER] SLEEPTIME=5 FILEPATH=/usr/bin/vim.tiny KSM_THRESHOLD=3 MESSAGE=\"Hello!\"\n");
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

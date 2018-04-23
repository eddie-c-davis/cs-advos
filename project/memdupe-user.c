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
        if (virt_on) {
            printf("<memdupe> Hypervisor flag is set, KVM is on\n");
        } else {
            printf("<memdupe> Error: Hypervisor flag is NOT set, KVM is off\n");
        }
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
        printf("<memdupe> Opened file: '%s'\n", path);

        /* Get file size */
        fseek(fp, 0, SEEK_END);
        *fsize = ftell(fp);
        rewind(fp);

        // Allocate buffer...
        printf("<memdupe> Allocating data: %ld bytes\n", *fsize);
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
        printf("<memdupe> Closed file: '%s'\n", path);
    } else {
        printf("<memdupe> Error opening file: '%s'\n", path);
        *fsize = 0;
    }

    return data;
}

static ulong write_pages(char** data, ulong pages, char chr) {
    ulong time1 = 0;
    ulong time2 = 0;

    /* Start timer for writing pages... */
    time1 = get_clock_time();

    do {
        (*data)[pages * MY_PAGE_SIZE - 1] = chr;
        pages--;
    } while (pages > 0);

    /* Stop timer */
    time2 = get_clock_time();

    return (time2 - time1);
}

static void free_data(char** data0, char **data1, char **data2) {
    free(*data0);
    free(*data1);
    free(*data2);
}

static int memdupe_init(void) {
    char *data0, *data1, *data2;

    uint vmx_on = FALSE;
    uint vm_stat = 0;
    uint cpl_flag = 0;

    ulong fsize;
    ulong pages;
    ulong wtime = 0;
    ulong w2time = 0;

    float ratio = 0.0;

    printf("<memdupe> In memdupe_init\n");

    /* Test virtualization */
    vmx_on = TRUE; //virt_test();

    /* Get CPL flag */
    cpl_flag = cpl_check();

    if (vmx_on && cpl_flag == CPL_USER) {
        /* Load a file */
        data0 = load_file(FILEPATH, &fsize);

        if (fsize > 0 && data0 != NULL) {
            pages = fsize / MY_PAGE_SIZE;
            printf("<memdupe> Read file of size %ld B, %ld pages\n", fsize, pages);

            /* Write pages once... */
            wtime = write_pages(&data0, pages, '.');
            printf("<memdupe> Wrote '.' to %ld pages once in %ld ns\n", pages, wtime);

            /* Load file 2 more times */
            data1 = load_file(FILEPATH, &fsize);
            data2 = load_file(FILEPATH, &fsize);
            printf("<memdupe> Read file '%s' 2 more times\n", FILEPATH);

            /* Sleep... */
            sleep(NUM_SECONDS);
            printf("<memdupe> Slept for %d seconds\n", NUM_SECONDS);

            /* Write pages again... */
            w2time = write_pages(&data0, pages, '.');
            printf("<memdupe> Wrote '.' to %ld pages again in %ld ns\n", pages, w2time);

            ratio = (float) w2time / (float) wtime;
            vm_stat = (ratio > KSM_THRESHOLD) ? TRUE : FALSE;

            printf("<memdupe> Ratio = %g = %ld / %ld, Threshold = %g, VM_Status = %d\n",
                   ratio, w2time, wtime, KSM_THRESHOLD, vm_stat);

            if (vm_stat) {
                /* KSM tells us we are running on a VM, create channel to other VM... */
                printf("data0 = %p, data1 = %p, data2 = %p", data0, data1, data2);
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

int main() {
    uint status;

    status = memdupe_init();
    memdupe_exit();

    return status;
}

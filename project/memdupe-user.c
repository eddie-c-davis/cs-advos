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

static ulong load_file(const char *path, char *data) {
    ulong size = 0;

//    struct file *fp;
//    struct inode *inode;
//    mm_segment_t fs;
//
//    // Open file
//    fp = filp_open(path, O_RDONLY, 0);
//
//    if (fp != NULL) {
//        printk("<memdupe> Opened file: '%s'\n", path);
//
//        //inode = fp->f_dentry->d_inode;
//        inode = fp->f_path.dentry->d_inode;
//        size = inode->i_size;
//
//        // Allocate buffer...
//        data = (char *) kmalloc(size + 1, GFP_ATOMIC);
//
//        if (data != NULL) {
//            printk("<memdupe> Reading file: '%s'\n", path);
//
//            fs = get_fs();
//            set_fs(KERNEL_DS);
//
//            fp->f_op->read(fp, data, size, &(fp->f_pos));
//            data[size] = '\0';  // Terminate string
//
//            // Restore segment descriptor
//            set_fs(fs);
//        } else {
//            printk("<memdupe> Error allocating data: %ld bytes\n", size);
//            size = 0;
//        }
//
//        // Close file
//        filp_close(fp, NULL);
//        printk("<memdupe> Closed file: '%s'\n", path);
//    } else {
//        printk("<memdupe> Error opening file: '%s'\n", path);
//    }

    return size;
}

static void write_pages(char* data, ulong pages, char chr) {
    do {
        data[pages*MY_PAGE_SIZE - 1] = chr;
        pages--;
    } while(pages > 0);
}

static void free_data(char* data[]) {
    uint i;
    for (i = 0; i <= NUM_READS; i++) {
        free(data[i]);
    }
}

static int memdupe_init(void) {
    char *data[NUM_READS+1];

    uint i;
    uint vmx_on = FALSE;
    uint thresh_stat = 0;
    uint cpl_flag = 0;

    ulong time1 = 0;
    ulong time2 = 0;
    ulong rtime = 0;
    ulong wtime = 0;
    ulong w2time = 0;
    ulong fsize;
    ulong pages;

    float ratio = 0.0;

    printf("<memdupe> In memdupe_init\n");

    /* Test virtualization */
    vmx_on = virt_test();

    /* Get CPL flag */
    cpl_flag = cpl_check();

    if (vmx_on && cpl_flag == CPL_USER) {
        /* Start timer */
        time1 = get_clock_time();

        /* Load a file */
        fsize = load_file(FILEPATH, data[0]);

        if (fsize > 0 && data[0] != NULL) {
            /* Stop timer */
            time2 = get_clock_time();
            rtime = time2 - time1;

            pages = fsize / MY_PAGE_SIZE;

            printf("<memdupe> File of size %ld B, %ld pages, read in %ld ns\n", fsize, pages, rtime);

            /* Restart timer for writing pages... */
            time1 = get_clock_time();

            /* Write pages once... */
            write_pages(data[0], pages, '.');

            /* Stop timer */
            time2 = get_clock_time();
            wtime = time2 - time1;

            printf("<memdupe> Wrote '.' to %ld pages once in %ld ns\n", pages, wtime);

            /* Load file NUM_READS more times */
            for (i = 1; i <= NUM_READS; i++) {
                fsize = load_file(FILEPATH, data[i]);
            }

            printf("<memdupe> Read file '%s' %d more times\n", FILEPATH, NUM_READS);

            /* Sleep my pretty... */
            sleep(NUM_SECONDS);

            printf("<memdupe> Slept for %d seconds\n", NUM_SECONDS);

            /* Restart timer for writing pages... */
            time1 = get_clock_time();

            /* Write pages again... */
            write_pages(data[0], pages, '.');

            /* Stop timer */
            time2 = get_clock_time();
            w2time = time2 - time1;

            printf("<memdupe> Wrote '.' to %ld pages again in %ld ns\n", pages, w2time);

            ratio = (float) w2time / (float) wtime;
            thresh_stat = (ratio > KSM_THRESHOLD) ? TRUE : FALSE;

            printf("<memdupe> Ratio = %g = %ld / %ld, Threshold = %g, Status = %d\n",
                   ratio, w2time, wtime, KSM_THRESHOLD, thresh_stat);

            // Avoid memory leaks...
            free_data(data);

            printf("<memdupe> Freed %d data pointers\n", NUM_READS+1);
        }
    }

    return thresh_stat;
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

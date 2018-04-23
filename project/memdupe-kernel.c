#include <linux/module.h>/* Needed by all modules */
#include <linux/kernel.h>/* Needed for KERN_INFO */
#include <linux/delay.h>    /* Sleep function */
#include <linux/fs.h>       /* File functions */
#include <linux/slab.h>     /* Mem functions */
#include <linux/time.h>     /* Timer functions */
#include <asm/uaccess.h>    /* Needed by segment descriptors */

#include "memdupe.h"

static void save_registers(void) {
    asm volatile("pushq %rcx\n"
            "pushq %rdx\n"
            "pushq %rax\n"
            "pushq %rbx\n"
    );
}

static void restore_registers(void) {
    asm volatile("popq %rbx\n"
            "popq %rax\n"
            "popq %rdx\n"
            "popq %rcx\n");
}

static int cpl_check(void) {
    uint csr, mask, cpl;
    asm("movl %%cs,%0" : "=r" (csr));

    mask = (1 << 2) - 1;
    cpl = csr & mask;

    if (cpl != CPL_KERN) {
        printk("<memdupe> Error: not running in kernel mode\n");
    }

    return cpl;
}

static int virt_test(void) {
    int cpuid_leaf = 1;
    int cpuid_ecx  = 0;
    int msr3a_value = 0;
    int vmx_on = FALSE;

    asm volatile("cpuid\n\t"
    :"=c"(cpuid_ecx)
    :"a"(cpuid_leaf)
    :"%rbx","%rdx");

    if((cpuid_ecx>>CPUID_VMX_BIT)&1) {
        printk("<memdupe> VMX supported CPU.\n");

        asm volatile("rdmsr\n"
            :"=a"(msr3a_value)
            :"c"(FEATURE_CONTROL_MSR)
            :"%rdx"
        );

        if(msr3a_value&1){
            if((msr3a_value>>2)&1){
                printk("<memdupe> MSR 0x3A:Lock bit is on.VMXON bit is on.OK\n");
                vmx_on = TRUE;
            } else {
                printk("<memdupe> MSR 0x3A:Lock bit is on.VMXONbit is off.Cannot do vmxon\n");
            }
        } else {
            printk("<memdupe> MSR 0x3A: Lock bit is not on. Not doing anything\n");
        }
    } else {
        printk("<memdupe> VMX not supported by CPU.\n");
    }

    return vmx_on;
}

static ulong get_clock_time(void) {
    struct timespec ts;
    ulong now;

    getnstimeofday(&ts);
    now = timespec_to_ns(&ts);

    return now;
}

static char *load_file(const char *path, ulong *fsize) {
    char *data;
    struct file *fp;
    struct inode *inode;
    mm_segment_t fs;

    // Open file
    fp = filp_open(path, O_RDONLY, 0);

    if (fp != NULL) {
        printk("<memdupe> Opened file: '%s'\n", path);

        /* Get file size */
        inode = fp->f_path.dentry->d_inode;
        *fsize = inode->i_size;

        // Allocate buffer...
        printk("<memdupe> Allocating data: %ld bytes\n", *fsize);
        data = (char *) kmalloc(*fsize + 1, GFP_ATOMIC);

        if (data != NULL) {
            printk("<memdupe> Reading file: '%s'\n", path);

            fs = get_fs();
            set_fs(KERNEL_DS);

            fp->f_op->read(fp, data, *fsize, &(fp->f_pos));
            data[*fsize] = '\0';  // Terminate string

            // Restore segment descriptor
            set_fs(fs);
        } else {
            printk("<memdupe> Error allocating data: %ld bytes\n", *fsize);
            *fsize = 0;
        }

        // Close file
        filp_close(fp, NULL);
        printk("<memdupe> Closed file: '%s'\n", path);
    } else {
        printk("<memdupe> Error opening file: '%s'\n", path);
        *fsize = 0;
    }

    return data;
}

static void write_pages(char** data, ulong pages, char chr) {
    do {
        (*data)[pages * MY_PAGE_SIZE - 1] = chr;
        pages--;
    } while (pages > 0);
}

static void free_data(char** data0, char **data1, char **data2) {
    kfree(*data0);
    kfree(*data1);
    kfree(*data2);
}

static int __init memdupe_init(void) {
    char *data0, *data1, *data2;

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

    printk("<memdupe> In memdupe_init\n");
    save_registers();

    /* Test virtualization */
    vmx_on = virt_test();

    /* Check CPL flag */
    cpl_flag = cpl_check();

    if (vmx_on && cpl_flag == CPL_KERN) {
        /* Start timer */
        time1 = get_clock_time();

        /* Load a file */
        data0 = load_file(FILEPATH, &fsize);

        if (fsize > 0 && data0 != NULL) {
            /* Stop timer */
            time2 = get_clock_time();
            rtime = time2 - time1;

            pages = fsize / MY_PAGE_SIZE;

            printk("<memdupe> File of size %ld B, %ld pages, read in %ld ns\n", fsize, pages, rtime);

            /* Restart timer for writing pages... */
            time1 = get_clock_time();

            /* Write pages once... */
            write_pages(&data0, pages, '.');

            /* Stop timer */
            time2 = get_clock_time();
            wtime = time2 - time1;

            printk("<memdupe> Wrote '.' to %ld pages once in %ld ns\n", pages, wtime);

            /* Load file 2 more times */
            data1 = load_file(FILEPATH, &fsize);
            data2 = load_file(FILEPATH, &fsize);

            printk("<memdupe> Read file '%s' 2 more times\n", FILEPATH);

            /* Sleep... */
            msleep(NUM_SECONDS * 1000);

            printk("<memdupe> Slept for %d seconds\n", NUM_SECONDS);

            /* Restart timer for writing pages... */
            time1 = get_clock_time();

            /* Write pages again... */
            write_pages(&data0, pages, '.');

            /* Stop timer */
            time2 = get_clock_time();
            w2time = time2 - time1;

            printk("<memdupe> Wrote '.' to %ld pages again in %ld ns\n", pages, w2time);

            ratio = (float) w2time / (float) wtime;
            thresh_stat = (ratio > KSM_THRESHOLD) ? TRUE : FALSE;

            printk("<memdupe> Ratio = %g = %ld / %ld, Threshold = %g, Status = %d\n",
                   ratio, w2time, wtime, KSM_THRESHOLD, thresh_stat);

            // Avoid memory leaks...
            free_data(&data0, &data1, &data2);

            printk("<memdupe> Freed data pointers\n");
        }
    }

    restore_registers();

    return thresh_stat;
}

static void __exit memdupe_exit(void) {
    printk("<memdupe> Done\n");
}

module_init(memdupe_init);
module_exit(memdupe_exit);

MODULE_AUTHOR("Eddie Davis <eddiedavis@boisestate.edu>");
MODULE_DESCRIPTION("KSM Memory Duplication Detector");
MODULE_LICENSE("GPL v2");

#include <linux/module.h>/* Needed by all modules */
#include <linux/kernel.h>/* Needed for KERN_INFO */
#include <linux/delay.h>    /* Sleep function */
#include <linux/fs.h>       /* File functions */
#include <linux/slab.h>     /* Mem functions */
#include <linux/time.h>     /* Timer functions */
#include <linux/uaccess.h>  /* Needed by segment descriptors */

#include "memdupe.h"

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

    asm volatile("pushq %rcx\n"
            "pushq %rdx\n"
            "pushq %rax\n"
            "pushq %rbx\n"
    );

    asm volatile("cpuid\n\t"
    :"=c"(cpuid_ecx)
    :"a"(cpuid_leaf)
    :"%rbx","%rdx");

    if((cpuid_ecx>>CPUID_VMX_BIT)&1) {
        asm volatile("rdmsr\n"
            :"=a"(msr3a_value)
            :"c"(FEATURE_CONTROL_MSR)
            :"%rdx"
        );

        if(msr3a_value&1){
            if((msr3a_value>>2)&1){
                vmx_on = TRUE;
            }
        }
    }

    asm volatile("popq %rbx\n"
            "popq %rax\n"
            "popq %rdx\n"
            "popq %rcx\n");

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
    char *data = NULL;
    struct file *fp;
    struct inode *inode;
    mm_segment_t fs;

    // Open file
    fp = filp_open(path, O_RDONLY, 0);

    if (fp != NULL) {
        /* Get file size */
        inode = fp->f_path.dentry->d_inode;
        *fsize = inode->i_size;

        // Allocate buffer...
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
    } else {
        printk("<memdupe> Error opening file: '%s'\n", path);
        *fsize = 0;
    }

    return data;
}

static ulong write_pages(char** data, ulong pages, uint step) {
    char *buffer;

    ulong index = 0;
    ulong time1 = 0;
    ulong time2 = 0;

    buffer = (char *) kmalloc(sizeof(char) * pages, GFP_ATOMIC);
    memset(buffer, '.', sizeof(char) * pages);
    strcpy(buffer, _message);

    /* Start timer for writing pages... */
    time1 = get_clock_time();

    do {
        (*data)[pages * MY_PAGE_SIZE - 1] = buffer[index];
        index++;
        pages--;
    } while (pages > 0);

    /* Stop timer */
    time2 = get_clock_time();

    kfree(buffer);

    return (time2 - time1);
}

static void free_data(ulong fsize, char** data0, char **data1, char **data2) {
    kfree(*data0);
    if (data1 != NULL) {
        kfree(*data1);
    }
    if (data2 != NULL) {
        kfree(*data2);
    }
}

static char *encode_message(char *msg, ulong *nbits) {
    char buff[BYTEBITS];
    char *bits;
    int i, j;
    uint index;
    uint nchars;

    nchars = strlen(msg);
    *nbits = nchars * BYTEBITS;

    bits = (char*) kmalloc(*nbits, GFP_ATOMIC);

    index = 0;
    for (i = 0; i < nchars; i++) {
        for (j = 0; j < BYTEBITS; j++) {
            buff[j] = (msg[i] & (BITMASK << j)) != 0;
        }

        for (j = BYTEBITS - 1; j >= 0; j--) {
            bits[index++] = buff[j];
        }
    }

    printk("encode_message: '%s' => ", msg);
    for (i = 0; i < *nbits; i++) {
        printk("%d", bits[i]);
        if (i % 8 == 7) {
            printk(" ");
        }
    }
    printk("\n");

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
    msg = (char *) kmalloc(nchars + 1, GFP_ATOMIC);

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

    printk("decode_message: %s\n", msg);

    return msg;
}

static int __init memdupe_init(void) {
    char *data0, *data1 = NULL, *data2 = NULL;

    uint vm_stat = 0;
    uint cpl_flag = 0;

    ulong fsize;
    ulong pages;
    ulong wtime = 0;
    ulong w2time = 0;
    ulong ratio = 0;

    _sleeptime = NUM_SECONDS;
    _vmrole = SENDER;
    strcpy(_filepath, FILEPATH);
    strcpy(_message, MESSAGE);
    _ksmthresh = KSM_THRESHOLD;
    _readtwice = TRUE;

    /* Check CPL flag */
    cpl_flag = cpl_check();

    if (cpl_flag == CPL_KERN) {
        printk("<memdupe> Running memdupe_init in kernel mode\n");

        /* Load a file */
        data0 = load_file(_filepath, &fsize);

        if (fsize > 0 && data0 != NULL) {
            pages = fsize / MY_PAGE_SIZE;
            printk("<memdupe> Read file of size %ld B, %ld pages\n", fsize, pages);

            /* Write pages once... */
            wtime = write_pages(&data0, pages, 1);
            printk("<memdupe> Wrote '.' to %ld pages once in %ld ns\n", pages, wtime);

            /* Load file 2 more times */
            if (_readtwice) {
                data1 = load_file(_filepath, &fsize);
                data2 = load_file(_filepath, &fsize);
                printk("<memdupe> Read file '%s' 2 more times\n", _filepath);
            }

            /* Sleep... */
            printk("<memdupe> Sleep for %d seconds\n", _sleeptime);
            msleep(_sleeptime * 1000);

            /* Write pages again... */
            w2time = write_pages(&data0, pages, 2);
            printk("<memdupe> Wrote '.' to %ld pages again in %ld ns\n", pages, w2time);

            ratio = w2time / wtime;
            vm_stat = (ratio > _ksmthresh) ? TRUE : FALSE;

            printk("<memdupe> Ratio = %ld = %ld / %ld, Threshold = %d, VM_Status = %d\n",
                   ratio, w2time, wtime, _ksmthresh, vm_stat);

            if (vm_stat) {
                printk("<memdupe> Memory deduplication probably occurred\n");
            } else {
                printk("<memdupe> Memory deduplication did not occur\n");
            }

            // Avoid memory leaks...
            free_data(fsize, &data0, &data1, &data2);
            printk("<memdupe> Freed data pointers\n");
        }
    }

    return vm_stat;
}

static void __exit memdupe_exit(void) {
    printk("<memdupe> Done\n");
}

module_init(memdupe_init);
module_exit(memdupe_exit);

MODULE_AUTHOR("Eddie Davis <eddiedavis@boisestate.edu>");
MODULE_DESCRIPTION("KSM Memory Duplication Detector");
MODULE_LICENSE("GPL v2");

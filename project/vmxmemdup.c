/*
* vmxmemdup.c - Detect memory duplication using KSM in KVM.
*/
#include <linux/module.h>/* Needed by all modules */
#include <linux/kernel.h>/* Needed for KERN_INFO */
//#include <linux/init.h>
//#include <linux/module.h>
//#include <linux/syscalls.h>
//#include <linux/fcntl.h>
#include <linux/fs.h>    /* File functions */
#include <linux/slab.h>  /* Mem functions */
#include <linux/time.h>  /* Timer functions */
#include <asm/uaccess.h>   // Needed by segment descriptors

//EXPORT_SYMBOL_GPL(show_regs);

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#define MY_PAGE_SIZE 4096

#define CPUID_VMX_BIT 5
#define FEATURE_CONTROL_MSR 0x3A
#define FILEPATH "/usr/bin/perl"

typedef unsigned int  uint;
typedef unsigned long ulong;

static void save_registers(void){
    asm volatile("pushq %rcx\n"
            "pushq %rdx\n"
            "pushq %rax\n"
            "pushq %rbx\n"
    );
}

static void restore_registers(void){
    asm volatile("popq %rbx\n"
            "popq %rax\n"
            "popq %rdx\n"
            "popq %rcx\n");
}

static int do_vmx_check(void) {
    int cpuid_leaf = 1;
    int cpuid_ecx  = 0;
    int msr3a_value = 0;
    int vmx_is_on = FALSE;

    asm volatile("cpuid\n\t"
    :"=c"(cpuid_ecx)
    :"a"(cpuid_leaf)
    :"%rbx","%rdx");

    if((cpuid_ecx>>CPUID_VMX_BIT)&1) {
        printk("<vmxmemdup> VMX supported CPU.\n");

        asm volatile("rdmsr\n"
            :"=a"(msr3a_value)
            :"c"(FEATURE_CONTROL_MSR)
            :"%rdx"
        );

        if(msr3a_value&1){
            if((msr3a_value>>2)&1){
                printk("<vmxmemdup> MSR 0x3A:Lock bit is on.VMXON bit is on.OK\n");
                vmx_is_on = TRUE;
            } else {
                printk("<vmxmemdup> MSR 0x3A:Lock bit is on.VMXONbit is off.Cannot do vmxon\n");
            }
        } else {
            printk("<vmxmemdup> MSR 0x3A: Lock bit is not on. Not doing anything\n");
        }
    } else {
        printk("<vmxmemdup> VMX not supported by CPU.\n");
    }

    return vmx_is_on;
}

static ulong get_day_time(void) {
    struct timespec ts;
    ulong now;

    getnstimeofday(&ts);
    now = timespec_to_ns(&ts);

    return now;
}

static ulong get_clock_time(void) {
    return get_day_time();
    // TODO: Fix this later maybe...
//    struct timespec ts;
//    ulong now;
//
//    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
//    now = timespec_to_ns(&ts);
//
//    return now;
}

static ulong load_file(const char *path, char *data) {
    ulong size = 0;

    struct file *fp;
    struct inode *inode;
    mm_segment_t fs;

    // Open file
    //fd = sys_open(path, O_RDONLY, 0);
    fp = filp_open(path, O_RDONLY, 0);

    //if (fd >= 0) {
    if (fp != NULL) {
        printk("<vmxmemdup> Opened file: '%s'\n", path);

        //inode = fp->f_dentry->d_inode;
        inode = fp->f_path.dentry->d_inode;
        size = inode->i_size;

        // Allocate buffer...
        data = (char *) kmalloc(size + 1, GFP_ATOMIC);

        if (data != NULL) {
            printk("<vmxmemdup> Reading file: '%s'\n", path);

            fs = get_fs();
            set_fs(KERNEL_DS);

            fp->f_op->read(fp, data, size, &(fp->f_pos));
            data[size] = '\0';  // Terminate string

            // Restore segment descriptor
            set_fs(fs);
        } else {
            printk("<vmxmemdup> Error allocating data: %ld bytes\n", size);
            size = 0;
        }

        // Close file
        //sys_close(fd);
        filp_close(fp, NULL);
        printk("<vmxmemdup> Closed file: '%s'\n", path);
    } else {
        printk("<vmxmemdup> Error opening file: '%s'\n", path);
    }

    return size;
}

//static void write_file(char* path, char* data) {
//    struct file *fp;
//    mm_segment_t fs;
//
//    fp = filp_open(path, O_RDWR | O_APPEND, 0644);
//    if(IS_ERR(fp)) {
//        printk("<vmxmemdup> Error opening file: '%s'\n", path);
//    } else {
//        fs = get_fs();
//        set_fs(KERNEL_DS);
//        fp->f_op->write(fp, data, strlen(data), &fp->f_pos);
//        set_fs(fs);
//        filp_close(fp, NULL);
//    }
//}

static void write_pages(char* data, ulong pages, char chr) {
    do {
        data[pages*MY_PAGE_SIZE - 1] = chr;
        pages--;
    } while(pages > 0);
}

static int __init vmxmemdup_init(void) {
    char *data = NULL;

    uint vmx_is_on = FALSE;

    ulong time1 = 0;
    ulong time2 = 0;
    ulong rtime = 0;
    ulong wtime = 0;
    ulong fsize;
    ulong pages;

    printk("<vmxmemdup> In vmxon\n");
    save_registers();

    /* Test virtualization */
    vmx_is_on = do_vmx_check();

    if (vmx_is_on) {
        /* Start timer */
        time1 = get_clock_time();

        /* Load a file */
        fsize = load_file(FILEPATH, data);

        if (fsize > 0 && data != NULL) {
            /* Stop timer */
            time2 = get_clock_time();
            rtime = time2 - time1;

            pages = fsize / MY_PAGE_SIZE;

            printk("<vmxmemdup> File of size %ld B, %ld pages, read in %ld ns\n", fsize, pages, rtime);

            /* Restart timer for writing pages... */
            time1 = get_clock_time();

            write_pages(data, pages, '.');

            /* Stop timer */
            time2 = get_clock_time();
            wtime = time2 - time1;

            // Avoid memory leaks...
            kfree(data);
        }
    }

    restore_registers();

    return 0;
}

static void __exit vmxmemdup_exit(void) {
    printk("<vmxmemdup> Done\n");
}

module_init(vmxmemdup_init);
module_exit(vmxmemdup_exit);

MODULE_AUTHOR("Eddie Davis <eddiedavis@boisestate.edu>");
MODULE_DESCRIPTION("VMX Memory Duplication Detector");
MODULE_LICENSE("GPL v2");


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

typedef unsigned char BOOL;
typedef unsigned int  UINT;
typedef unsigned long ULONG;

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

static ULONG get_day_time(void) {
    struct timespec ts;
    ULONG now;

    getnstimeofday(&ts);
    now = timespec_to_ns(&ts);

    return now;
}

static ULONG get_clock_time(void) {
    return get_day_time();
    // TODO: Fix this later maybe...
//    struct timespec ts;
//    ULONG now;
//
//    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
//    now = timespec_to_ns(&ts);
//
//    return now;
}

static ULONG load_file(const char *path, char *data) {
    ULONG size = 0;

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




static int __init vmxmemdup_init(void) {
    char *content = NULL;

    BOOL vmx_is_on = FALSE;

    ULONG start_time = 0;
    ULONG stop_time = 0;
    ULONG read_time = 0;
    ULONG file_size;

    printk("<vmxmemdup> In vmxon\n");
    save_registers();

    /* Test virtualization */
    vmx_is_on = do_vmx_check();

    if (vmx_is_on) {
        /* Start timer */
        start_time = get_clock_time();

        /* Load a file */
        file_size = load_file(FILEPATH, content);

        /* Stop timer */
        stop_time = get_clock_time();
        read_time = stop_time - start_time;

        printk("<vmxmemdup> File of size %ld read in %ld ns\n", file_size, read_time);
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


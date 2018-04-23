/*
* memdupe - Detect memory duplication using KSM in KVM.
*/

#ifndef _MEMDUPE_H_
#define _MEMDUPE_H_

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#define BILLION      1000000000
#define BUFFER_SIZE  4096
#define MY_PAGE_SIZE 4096

#define CPUID_VMX_BIT 5
#define FEATURE_CONTROL_MSR 0x3A
#define FILEPATH "/usr/bin/perl"

#define NUM_READS     2
#define NUM_SECONDS   3
#define KSM_THRESHOLD 2

#define CPL_KERN 0
#define CPL_USER 3

#define MESSAGE "Here is the message to be sent through the covert channel"

typedef unsigned int  uint;
typedef unsigned long ulong;

static int virt_test(void);
static int cpl_check(void);

static ulong get_clock_time(void);
static char *load_file(const char *path, ulong *fsize);
static ulong write_pages(char** data, ulong pages, char *msg);
static void free_data(char** data0, char **data1, char **data2);

#endif

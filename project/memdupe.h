/**
 * @author Eddie Davis
 * @project memdupe
 * @file memdupe.h
 * @brief Detect memory duplication using KSM in KVM.
 * @date 4-25-2018
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
#define FILEPATH "/usr/bin/vim.tiny"

#define NUM_READS     2
#define NUM_SECONDS   5
#define KSM_THRESHOLD 3

#define CPL_KERN 0
#define CPL_USER 3

#define MESSAGE "Hello!"

#define BITMASK  1
#define BYTEBITS 8

/* VM Roles */
#define TESTER   0
#define SENDER   1
#define RECEIVER 2

typedef unsigned int  uint;
typedef unsigned long ulong;

static char _filepath[1024];
static char _message[1024];

static int _vmrole;
static int _sleeptime;
static int _ksmthresh;
static int _readtwice;

static int virt_test(void);
static int cpl_check(void);
static ulong get_clock_time(void);
static char *load_file(const char *path, ulong *fsize);
static ulong write_pages(char** data, ulong pages, uint step);
static char *encode_message(char *msg, ulong *nbits);
static char *decode_message(char *bits, ulong nbits);
static void free_data(ulong fsize, char** data0, char **data1, char **data2);

#endif

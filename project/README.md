#memdupe: This project contains the source code for the memdupe KSM virtualization covert channel utility

##Building
1. Run the _make_ command to trigger compilation.

```
$ make
make -C /lib/modules/4.14.12-dev/build M=/Work/AdvOS/kernel/labs/project modules
make[1]: Entering directory '/Work/AdvOS/kernel/linux-4.14.12'
  Building modules, stage 2.
  MODPOST 1 modules
make[1]: Leaving directory '/Work/AdvOS/kernel/linux-4.14.12'
gcc memdupe.c -g -o memdupe -Wunused-function

##Running

```
2. Run the user space program, use -h argument to get the usage statement, or run with no arguments to use the default values.

```
$ ./memdupe -h
usage: memdupe ROLE[0=TESTER|1=SENDER|2=RECEIVER] SLEEPTIME=5 FILEPATH=/usr/bin/vim.tiny KSM_THRESHOLD=3 MESSAGE="Hello!"
```

3. To load the kernel module, use the following command.

```
$ sudo insmod kmemdupe
```

The output can be read with the _dmesg_ command.

```
<memdupe> Running memdupe_init in host mode
<memdupe> Reading file: '/usr/bin/vim.tiny'
<memdupe> Read file of size 1064592 B, 259 pages
<memdupe> Reading file: '/usr/bin/vim.tiny'
<memdupe> Reading file: '/usr/bin/vim.tiny'
<memdupe> Read file '/usr/bin/vim.tiny' 2 more times
<memdupe> Wrote 259 pages once in 12349 ns
<memdupe> Sleep for 5 seconds
<memdupe> Wrote 259 pages again in 24036 ns
<memdupe> Ratio = 1.94639 = 24036 / 12349, Threshold = 3, VM_Status = 0
<memdupe> Memory deduplication did not occur
<memdupe> Freed data pointers
<memdupe> Done
```

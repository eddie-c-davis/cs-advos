obj-m += cplkernel.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	gcc cpluser.c -g -o cpluser
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm cpluser

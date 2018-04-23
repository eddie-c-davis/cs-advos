obj-m += memdupe-kernel.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	gcc memdupe-user.c -g -o memdupe-user
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f memdupe-user

obj-m += kmemdupe.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	gcc memdupe.c -g -o memdupe -Wunused-function
user:
	gcc memdupe.c -O3 -g -o memdupe -Wunused-function
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f memdupe

PROGS = init

all: ${PROGS}

${PROGS}: init.o machine.o
	ld -e start -nostdlib -no-pie -o $@ init.o machine.o

%.o: Makefile %.c
	gcc -O3 -fno-pic -nostdlib -MD -c -o $@ $*.c

%.o: Makefile %.S
	gcc -nostdinc -nostdlib -MD -c -o $@ $*.S

clean:
	rm -rf *.d *.o init

-include *.d

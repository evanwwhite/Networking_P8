PROGS = init

C_FILES = ${wildcard *.c}
S_FILES = ${wildcard *.S}

C_O_FILES = ${filter-out ${PROGS}.o, ${C_FILES:.c=.o}}
S_O_FILES = ${S_FILES:.S=.o}

O_FILES = ${C_O_FILES} ${S_O_FILES}

all : ${PROGS}


${PROGS} : % : %.o ${O_FILES}
	ld -e start -nostdlib -no-pie -o $@ $*.o ${O_FILES}

%.o : Makefile  %.c
	gcc -O3 -MD -fno-pic -nostdlib -MD -c -o $@ $*.c

%.o : Makefile  %.S
	gcc -MD -nostdinc -nostdlib -MD -c -o $@ $*.S

clean :
	rm -rf *.d *.o

-include *.d

PROGS = init
MAIN = ams25683

C_FILES = ${wildcard *.c}
S_FILES = ${wildcard *.S}

C_O_FILES = ${C_FILES:.c=.o}
S_O_FILES = ${S_FILES:.S=.o}

O_FILES = ${filter-out ${MAIN}.o, ${C_O_FILES}} ${S_O_FILES}

all : ${PROGS}

${PROGS} : % : ${MAIN}.o ${O_FILES}
	ld -e start -nostdlib -no-pie -o $@ ${MAIN}.o ${O_FILES}

%.o : %.c Makefile
	gcc -O3 -fno-pic -fno-builtin -nostdlib -MD -c -o $@ $<

%.o : %.S Makefile
	gcc -MD -nostdinc -nostdlib -c -o $@ $<

clean :
	rm -rf *.d *.o init

-include *.d

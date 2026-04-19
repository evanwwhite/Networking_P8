PROGS = init

C_FILES = ${wildcard *.c}
S_FILES = ${wildcard *.S}

C_O_FILES = ${filter-out ${PROGS}.o, ${C_FILES:.c=.o}}
S_O_FILES = ${S_FILES:.S=.o}

O_FILES = ${C_O_FILES} ${S_O_FILES}

all : ${PROGS} respect_readme # modified

# modified
${PROGS} : % : %.o ${O_FILES}
	ld -e start -nostdlib -no-pie -T custom.ld -o $@ $*.o ${O_FILES}

# modified
%.o : Makefile %.c
	gcc -O3 -MD -fno-pic -mcmodel=large -nostdlib -c -o $@ $*.c

# modified
%.o : Makefile %.S
	gcc -MD -nostdinc -nostdlib -mcmodel=large -c -o $@ $*.S

clean :
	rm -rf *.d *.o

-include *.d

# added
respect_readme: ${PROGS} 
	mkdir -p sbin
	cp ${PROGS} sbin/
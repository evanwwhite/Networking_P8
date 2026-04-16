SHELL := $(shell which bash)
TEST_EXTS = .ok .dir
UTCS_ID ?= $(shell pwd | sed -e 's/.*_//')
UTCS_OPTS ?= -O2

MY_TESTS = ${addprefix ${UTCS_ID},${TEST_EXTS}}

TESTS_DIR ?= .

POSSIBLE_TESTS = ${notdir ${basename ${wildcard ${TESTS_DIR}/*${firstword ${TEST_EXTS}}}}}
TESTS = ${sort ${POSSIBLE_TESTS}}
TEST_OKS = ${addsuffix .ok,${TESTS}}
TEST_CYCLES = ${addsuffix .cycles,${TESTS}}
TEST_RESULTS = ${addsuffix .result,${TESTS}}
TEST_TARGETS = ${addsuffix .test,${TESTS}}
TEST_OUTS = ${addsuffix .out,${TESTS}}
TEST_RAWS = ${addsuffix .raw,${TESTS}}
TEST_DATA = ${addsuffix .data,${TESTS}}
TEST_DIFFS = ${addsuffix .diff,${TESTS}}
TEST_LOOPS = ${addsuffix .loop,${TESTS}}
TEST_FAILS = ${addsuffix .fail,${TESTS}}
TEST_IMAGES = ${addprefix build/, ${addsuffix .img,${TESTS}}}

ORIGIN_REPO=${shell git config --get remote.origin.url | sed -e s'/.git$$//'}
STUDENT_NAME=${shell echo ${ORIGIN_REPO} | sed -e 's/.*_//'}
TESTS_REPO=${shell echo ${ORIGIN_REPO} | sed -e 's/_${STUDENT_NAME}/__tests/'}

SRCFILES := $(shell find kernel -type f 2>/dev/null | sort)
CFILES := $(filter %.c,$(SRCFILES))
CCFILES := $(filter %.cc,$(SRCFILES))
SFILES := $(filter %.S,$(SRCFILES))
OFILES := $(addprefix build/,$(CFILES:.c=.c.o) $(CCFILES:.cc=.cc.o) $(SFILES:.S=.S.o))

CC := gcc
CXX := g++
LD := g++

CFLAGS := ${UTCS_OPTS} -std=gnu23 -Wall -Werror @common.flags
CCFLAGS := ${UTCS_OPTS} -std=gnu++23 -Wall -Werror @common.flags
LDFLAGS = \
    @common.flags \
    -nostdlib \
    -static \
    -T script.ld

# customize by setting environment variables
QEMU_ACCEL ?= tcg,thread=multi
QEMU_CPU ?= max
QEMU_SMP ?= 4
QEMU_MEM ?= 128m
QEMU_TIMEOUT ?= 10
QEMU_TIMEOUT_CMD ?= timeout
QEMU_DEBUG ?= #-d int

QEMU_PREFER = ~gheith/public/tools26/bin/qemu-system-x86_64
QEMU_CMD ?= ${shell (test -x ${QEMU_PREFER} && echo ${QEMU_PREFER}) || echo qemu-system-x86_64}

QEMU_CONFIG_FLAGS = -accel ${QEMU_ACCEL} \
                    -machine q35 \
	            -cpu ${QEMU_CPU} \
	            -smp ${QEMU_SMP} \
	            -m ${QEMU_MEM} \
	            ${QEMU_DEBUG}

QEMU_FLAGS = -no-reboot \
	     ${QEMU_CONFIG_FLAGS} \
	     -nographic\
	     --monitor none \
	     --serial file:$*.raw \
	     -drive file=build/$*.img,index=0,media=disk,format=raw,file.locking=off \
             -drive file=$*.data,index=3,media=disk,format=raw,file.locking=off \
	     -device isa-debug-exit,iobase=0xf4,iosize=0x04

TIME = $(shell which time)

.PHONY: ${TESTS} sig test tests all clean ${TEST_TARGETS} help qemu_config_flags qemu_cmd before_test 

all : ${TESTS};

help:
	@echo ""
	@echo "Makefile for ${ORIGIN_URL}"
	@echo
	@echo "Useful targets:"
	@echo "    make -s all             # build images for all tests"
	@echo "    make -s t0              # build image for t0"
	@echo "    make -s t0.test         # run t0 and report results"
	@echo "    make -s t0.loop         # run t0 10 times and report results"
	@echo "    make -s t0.fail         # run t0 until it fails (max LOOP_LIMIT times)"
	@echo "    make -s test            # run all tests once"
	@echo "    make -s test.loop       # use only if you absolutely have to"
	@echo "                            # and after checking that no one else"
	@echo "                            # is using this machine"
	@echo "    make -s get_tests       # get (or update) peer tests from the server"
	@echo "                            # saves the tests in ./all_tests"
	@echo "                            # please don't push back to git"
	@echo "    make -s get_results     # get (or update) your reuslts from the server"
	@echo "                            # saves the reulsts in ./my_results"
	@echo "                            # please don't push back to git"
	@echo "Environment Variables:"
	@echo "    number of loop iterations: LOOP_LIMIT       (${LOOP_LIMIT})"
	@echo "    qemu acceleration flag   : QEMU_ACCEL       (${QEMU_ACCEL})"
	@echo "    qemu command             : QEMU_CMD         (${QEMU_CMD})"
	@echo "    qemu cpu                 : QEMU_CPU         (${QEMU_CPU})"
	@echo "    simulated memory         : QEMU_MEM         (${QEMU_MEM})"
	@echo "    number of cores          : QEMU_SMP         (${QEMU_SMP})"
	@echo "    timeout                  : QEMU_TIMEOUT     (${QEMU_TIMEOUT})"
	@echo "    timeout command          : QEMU_TIMEOUT_CMD (${QEMU_TIMEOUT_CMD})"
	@echo "    tests directory          : TESTS_DIR        (${TESTS_DIR})"
	@echo ""
	@echo "tests: ${TESTS}"

origin:
	@echo "repo       : ${ORIGIN_REPO}"
	@echo "student    : ${STUDENT_NAME}"
	@echo "tests repo : ${TESTS_REPO}"

get_tests:
	test -d all_tests || git clone ${TESTS_REPO} all_tests
	(cd all_tests ; git pull)
	@echo ""
	@echo "Tests copied to all_tests (cd all_tests)"
	@echo "   Please don't add the all_tests directory to git"
	@echo ""

get_summary:
	test -d all_results || git clone ${GIT_SERVER}:${PROJECT_NAME}__results all_results
	(cd all_results ; git pull)
	python tools/summarize.py all_results

get_results:
	test -d my_results || git clone ${ORIGIN_REPO}_results my_results
	(cd my_results ; git pull)
	@(cd my_results;                                                      \
		for i in *.result; do                                         \
			name=$$(echo $$i | sed -e 's/\..*//');                \
			echo "$$name `cat $$name.result` `cat $$name.time`";  \
		done;                                                         \
		echo "";                                                      \
		echo "`grep pass *.result | wc -l` / `ls *.result | wc -l`";  \
	)
	@echo ""
	@echo "More details in my_results (cd my_results)"
	@echo "    Please don't add my_results directory to git"
	@echo ""

qemu_cmd:
	@echo "${QEMU_CMD}"

qemu_config_flags:
	@echo "${QEMU_CONFIG_FLAGS}"

$(TESTS) : % : build/%.img;

clean:
	rm -rf *.diff *.raw *.out *.result *.failure *.time build *.debug *.cycles *.data
	make -C limine clean

build/%.c.o: %.c Makefile common.flags
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

build/%.cc.o: %.cc Makefile common.flags
	@mkdir -p "$(dir $@)"
	$(CXX) -I${shell pwd} $(CCFLAGS) $(CPPFLAGS) -c $< -o $@

build/%.S.o: %.S Makefile common.flags
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

build/kernel.elf: common.flags Makefile script.ld $(OFILES)
	@mkdir -p build
	$(LD) $(LDFLAGS) $(OFILES) -o $@

# Compilation rules for *.S files.
build/%.S.o: %.S Makefile common.flags
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

limine/limine: Makefile limine/*.c limine/*.h limine/Makefile
	make -C limine

LIMINE_FILES := limine.conf limine/limine-bios.sys limine/BOOTX64.EFI


${TEST_IMAGES} : build/%.img: build/kernel.elf Makefile limine/limine %.data ${LIMINE_FILES}
	# borrowed from https://codeberg.org/Limine/limine-cxx-template/src/branch/trunk/GNUmakefile
	rm -f $@
	# zero-filled 20MB raw disk image
	dd if=/dev/zero bs=1M seek=20 count=0 of=$@ > image.debug 2>&1
	# create a boot partition
	PATH=$$PATH:/usr/sbin:/sbin sgdisk $@ -n 1:2048 -t 1:ef00 -m 1 >> image.debug 2>&1
	# install BIOS boot sector and stage2
	./limine/limine bios-install $@ >> image.debug 2>&1
	# format the rest as a FAT32 file system (Limine requirement)
	mformat -i $@@@1M >> image.debug 2>&1
	# create required directories (UEFI and Limine specified)
	mmd -i $@@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine >> image.debug 2>&1
	# copy the kernel executable
	mcopy -i $@@@1M build/kernel.elf ::/boot/kernel >> image.debug 2>&1
	# copy the ramdisk image
	mcopy -i $@@@1M $*.data ::/boot/ramdisk >> image.debug 2>&1
	# copy the Limine required files
	mcopy -i $@@@1M ${LIMINE_FILES} ::/boot/limine >> image.debug 2>&1
	touch $@

${TEST_RAWS} : %.raw : Makefile %
	@rm -f $*.raw $*.failure
	@touch $*.failure
	@echo "*** failed to run, look in $*.failure for more details" > $*.raw
	-(${TIME} --quiet -o $*.time -f "%E" ${QEMU_TIMEOUT_CMD} ${QEMU_TIMEOUT} ${QEMU_CMD} ${QEMU_FLAGS} > $*.failure 2>&1); if [ $$? -eq 124 ]; then echo "timeout" > $*.failure; echo "timeout" > $*.time; fi
	@touch $*.cycles
	@-egrep '^@@@ ' $*.raw > $*.cycles 2>&1

BLOCK_SIZE = 4096

${TEST_DATA} : %.data : Makefile
	@rm -f $*.data
	mkfs.ext2 -q -b ${BLOCK_SIZE} -i ${BLOCK_SIZE} -d ${TESTS_DIR}/$*.dir  -I 128 -r 0 -t ext2 $*.data 10m

${TEST_OUTS} : %.out : Makefile %.raw
	-egrep '^\*\*\*' $*.raw > $*.out 2> /dev/null || true

${TEST_DIFFS} : %.diff : Makefile %.out ${TESTS_DIR}/%.ok
	-(diff -wBb $*.out ${TESTS_DIR}/$*.ok > $*.diff 2> /dev/null || true)

${TEST_RESULTS} : %.result : Makefile %.diff
	(test -z "`cat $*.diff`" && echo "pass" > $*.result) || echo "fail" > $*.result

${TEST_TARGETS} : %.test : Makefile %.result
	@echo -n "$* ... "
	@echo "`cat $*.result` `cat $*.time`"

OTHER_USERS = ${shell who | sed -e 's/ .*//' | sort | uniq}
HOW_MANY = ${shell who | sed -e 's/ .*//' | sort | uniq | wc -l}
LOOP_LIMIT ?= 10

loop_warning.%:
	@echo "*******************************************************************************"
	@echo "*** This is NOT the sort of thing you run ALL THE TIME on a SHARED MACHINE  ***"
	@echo "*** In particular long running tests and tests that timeout                 ***"
	@echo "*******************************************************************************"
	@echo ""
	@echo "You can use LOOP_LIMIT to control the number if iterations. For example:"
	@echo "   LOOP_LIMIT=7 make -s $*.loop"
	@echo ""
	@echo "::::::: You are 1 of ${HOW_MANY} users on this machine"
	@echo ":::::::         ${OTHER_USERS}"
	@echo ":::::::   all of them value their work and their time as much as you value yours"
	@echo ":::::::"
	@echo ""


${TEST_LOOPS} : %.loop : loop_warning.% %
	@let pass=0; \
	for ((i=1; i<=${LOOP_LIMIT}; i++)); do \
		echo -n  "[$$i/${LOOP_LIMIT}] "; \
		$(MAKE) -s $*.test; \
		if [ "`cat $*.result`" = "pass" ]; then let pass=pass+1; fi; \
	done; \
	echo ""; \
	echo "$$(basename $$(pwd)) $* $$pass/${LOOP_LIMIT}"; \
	echo ""

${TEST_FAILS} : %.fail : loop_warning.% %
	@let pass=0; \
	for ((i=1; i<=${LOOP_LIMIT}; i++)); do \
		echo -n  "[$$i/${LOOP_LIMIT}] "; \
		$(MAKE) -s $*.test; \
		if [ "`cat $*.result`" = "pass" ]; then let pass=pass+1; else break; fi; \
	done; \
	echo ""; \
	echo "$$(basename $$(pwd)) $* $$pass/${LOOP_LIMIT}"; \
	echo ""	

before_test:
	rm -f *.result *.time *.out *.raw *.failure *.cycles

test: before_test Makefile ${TESTS} ${TEST_TARGETS} ;
	-@echo ""
	-@echo -n "$$(basename $$(pwd)) "
	-@echo "pass:`(grep pass *.result | wc -l) || echo 0`/`(ls *.result | wc -l) || echo 0`"
	-@echo ""

test.loop: loop_warning.test ${TEST_LOOPS}

failed:
	-@for i in "`grep -l fail *.result`"; do \
		t=`echo $$i | sed -e "s/\.result//"`; \
		echo ""; \
		echo "**************** $$t ****************"; \
		cat $$t.diff; \
	done

format:
	clang-format -i kernel/*.cc
	clang-format -i kernel/*.h


-include ${shell find build -name '*.d' 2> /dev/null}
-include *.d


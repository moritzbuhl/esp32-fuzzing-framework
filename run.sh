#!/bin/sh

set -eu
threads=${2:-1}
indir=$(mktemp -d)

IMG=./example_esp32_server/tcp_server.img
ELF=./example_esp32_server/build/tcp_server.elf

HFUZZ="./honggfuzz/honggfuzz -t 20 -i $indir -u -n $threads -F 127 --"
QEMU="./qemu/xtensa-softmmu/qemu-system-xtensa -nographic -machine esp32 -drive file=$IMG,if=mtd,format=raw -global driver=timer.esp32.timg,property=wdt_disable,value=true"
ESP32_GDB=~/.espressif/tools/xtensa-esp32-elf/esp-2022r1-11.2.0/xtensa-esp32-elf/bin/xtensa-esp32-elf-gdb

case ${1:-} in
	whitebox)
	mem_dump=./dump.bin
	init=$(grep -A1 listen tcp_server_task_disass.txt | tail -1 |awk '{ print $1}')
	start=$(head -2 processData_disass.txt  | tail -1  | awk '{print $2 }')
	end=$(tail -2 processData_disass.txt  | head -1  | awk '{print $1 }')
	$HFUZZ $QEMU \
		-nic user,model=open_eth,hostfwd=tcp::?-:80  \
		-whitebox init=$init,start=$start,end=$end
	;;
	blackbox)
	mem_dump=./dump.bin
	regs_dump=./regs_dump.txt
	len_reg=$(grep 127 regs_dump.txt  | awk '{printf "+"$1}' | cut -d+ -f2-)
	data_buf=$( awk '{print $4}' bt.txt  | head -1 | cut -d= -f2)
	setup=$(head -2 app_main_disass.txt  | tail -1  | awk '{print $2 }')
	entry=$(head -2 processData_disass.txt  | tail -1  | awk '{print $2 }')
	exit=$(tail -2 processData_disass.txt  | head -1  | awk '{print $1 }')
	$HFUZZ $QEMU \
		-nic user,model=open_eth \
		-blackbox setup=$setup,entry=$entry,exit=$exit,len=$len_reg,data=$data_buf,dump_file=$mem_dump,regs_file=$regs_dump
	;;
	debug)
	$QEMU \
		-nic user,model=open_eth,hostfwd=tcp::8080-:80  \
		-s -S &
	$ESP32_GDB -ex 'target remote:1234' \
		-ex 'set confirm off' \
		-ex "file $ELF"
	;;
	dump)
	> tcp_server_task_disass.txt
	> main_task_disass.txt
	> app_main_disass.txt
	> processData_disass.txt
	> string_functions.txt
	> regs_dump.txt
	> bt.txt
	regdump=$(mktemp)
	$QEMU \
		-nic user,model=open_eth,hostfwd=tcp::8080-:80  \
		-s -S &
	sleep 3 &&  printf "GET /HALLO%115.s\r\n" | nc 127.0.0.1 8080 &
	$ESP32_GDB -ex 'target remote:1234' \
		-ex 'set confirm off' \
		-ex "file $ELF" \
		-ex 'b *processData' \
		-ex 'c' \
		-ex 'set pagination off' \
		-ex "set logging file $regdump" \
		-ex 'set logging on' \
		-ex 'i reg' \
		-ex 'set logging off' \
		-ex 'set logging file tcp_server_task_disass.txt' \
		-ex 'set logging on' \
		-ex 'disass tcp_server_task' \
		-ex 'set logging off' \
		-ex 'set logging file main_task_disass.txt' \
		-ex 'set logging on' \
		-ex 'disass main_task' \
		-ex 'set logging off' \
		-ex 'set logging file app_main_disass.txt' \
		-ex 'set logging on' \
		-ex 'disass app_main' \
		-ex 'set logging off' \
		-ex 'set logging file processData_disass.txt' \
		-ex 'set logging on' \
		-ex 'disass processData' \
		-ex 'set logging off' \
		-ex 'dump binary memory dump.bin 0x3FF80000 0x3FFFFFFF' \
		-ex 'set logging file string_functions.txt' \
		-ex 'set logging on' \
		-ex 'echo strcmp' \
		-ex 'p &strcmp' \
		-ex 'echo strncmp' \
		-ex 'p &strncmp' \
		-ex 'echo strcasecmp' \
		-ex 'p &strcasecmp' \
		-ex 'echo strncasecmp' \
		-ex 'p &strncasecmp' \
		-ex 'set logging off' \
		-ex 'set logging file bt.txt' \
		-ex 'set logging on' \
		-ex 'bt' \
		-ex 'set logging off' \
		-ex 'kill' \
		-ex 'q'
		tail -16 $regdump > regs_dump.txt
		rm $regdump
		;;
	*)
		echo "usage: $0 [whitebox | blackbox | debug | dump] [threads]"
esac

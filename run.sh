#!/bin/sh

set -eu
threads=${2:-1}
indir=$(mktemp -d)

ESP32_GDB=~/.espressif/tools/xtensa-esp32-elf/esp-2022r1-11.2.0/xtensa-esp32-elf/bin/xtensa-esp32-elf-gdb
HFUZZ="./honggfuzz/honggfuzz -t 20 -i $indir -u -n $threads -F 127 --"
QEMU="./qemu/xtensa-softmmu/qemu-system-xtensa -nographic -machine esp32 -drive file=./example_esp32_server/tcp_server.img,if=mtd,format=raw -global driver=timer.esp32.timg,property=wdt_disable,value=true"

setup=$(echo "0x$(readelf ./example_esp32_server/build/tcp_server.elf -s | grep app_main | awk '{ print $2 }')")

len_reg=a5+a10+a15
data_buf=0x3ffbbc4c
mem_dump=./example_esp32_server/dump.bin
regs_dump=./example_esp32_server/regs_dump.txt

case ${1:-} in
	whitebox)
	mem_dump=./dump.bin
	#regs_dump=./regs_dump.txt
	init=$setup
	start=0x400d0020 #$(head -2 processData_disass.txt  | tail -1  | awk '{print $2 }')
	end=0x401022bc #$(tail -2 processData_disass.txt  | head -1  | awk '{print $1 }')
	entry=$start
	exit=$end
	$HFUZZ $QEMU \
		-nic user,model=open_eth,hostfwd=tcp::?-:80  \
		-whitebox init=$init,start=$start,end=$end \
		#-blackbox setup=$setup,entry=$entry,exit=$exit,len=$len_reg,data=$data_buf,dump_file=$mem_dump,regs_file=$regs_dump
	;;
	blackbox)
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
	$ESP32_GDB -ex 'target remote:1234'
	;;
	dump)
	> processData_disass.txt
	> regs_dump.txt
	regdump=$(mktemp)
	$QEMU \
		-nic user,model=open_eth,hostfwd=tcp::8080-:80  \
		-s -S &
	sleep 4 &&  printf "GET /HALLO%115.s\r\n" | nc 127.0.0.1 8080 &
	$ESP32_GDB -ex 'target remote:1234' \
		-ex 'set confirm off' \
		-ex 'file ./example_esp32_server/build/tcp_server.elf' \
		-ex 'b *processData' \
		-ex 'c' \
		-ex 'set pagination off' \
		-ex "set logging file $regdump" \
		-ex 'set logging on' \
		-ex 'i reg' \
		-ex 'set logging off' \
		-ex 'set logging file processData_disass.txt' \
		-ex 'set logging on' \
		-ex 'disass processData' \
		-ex 'set logging off' \
		-ex 'dump binary memory dump.bin 0x3FF80000 0x3FFFFFFF' \
		-ex 'kill' \
		-ex 'q'
		tail -16 $regdump > regs_dump.txt
		rm $regdump
		;;
	*)
		echo "usage: $0 [whitebox | blackbox | debug | dump] [threads]"
esac

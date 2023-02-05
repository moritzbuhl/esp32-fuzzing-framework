#!/bin/sh

set -x
#cd esp32fuzzing/esp32-fuzzing-framework/
# attach with debugger:
# ~/.espressif/tools/xtensa-esp32-elf/esp-2022r1-11.2.0/xtensa-esp32-elf/bin/xtensa-esp32-elf-gdb -ex 'target remote:1234'
# >file example_esp32_server/build/tcp_server.elf

threads=${2:-1}
indir=$(mktemp -d)

# get entry, exit, etc:
# b *processData
# c
# disass processData, the last number
# i reg
# dump binary memory dump.bin 0x3FF80000 0x3FFFFFFF 

setup=$(echo "0x$(readelf ./example_esp32_server/build/tcp_server.elf -s | grep app_main | awk '{ print $2 }')")

# for whitebox: maybe
#setup=0x400d6b61

#$(echo "0x$(readelf ./example_esp32_server/build/tcp_server.elf -s | grep processData | awk '{ print $2 }')")
entry=0x400d68c4
#$(echo "0x$(readelf ./example_esp32_server/build/tcp_server.elf -s | grep -A1 processData | tail -1 | awk '{ print $2 }')") - 1
exit=0x400d69bc
#exit=0x400d69c0 # 1 too much for blackbox fuzzing!


case $1 in
	whitebox)
./honggfuzz/honggfuzz -t 150 -i $indir -u -n $threads -F 127 -- \
./qemu/xtensa-softmmu/qemu-system-xtensa -nographic -machine esp32 -drive file=./example_esp32_server/tcp_server.img,if=mtd,format=raw -global driver=timer.esp32.timg,property=wdt_disable,value=true -nic user,model=open_eth,hostfwd=tcp::?-:80  \
-whitebox setup=$setup,start=$entry,end=$exit
	;;
	blackbox)
./honggfuzz/honggfuzz -i $indir -t 90 -u -n $threads -F 127 -- \
./qemu/xtensa-softmmu/qemu-system-xtensa -nographic -machine esp32 -drive file=./example_esp32_server/tcp_server.img,if=mtd,format=raw -global driver=timer.esp32.timg,property=wdt_disable,value=true -nic user,model=open_eth \
-blackbox setup=$setup,entry=$entry,exit=$exit,len=a5,data=0x3ffbbc30,dump_file=./example_esp32_server/dump.bin,regs_file=./example_esp32_server/regs_dump.txt
esac

# debugging stuf:
#hostfwd=tcp::8081-:80  \
#-s -S

# run with self built xtensa qemu release from 2020. for debugging
#../qemu/build/xtensa-softmmu/qemu-system-xtensa -nographic -machine esp32 -drive file=./example_esp32_server/tcp_server.img,if=mtd,format=raw -global driver=timer.esp32.timg,property=wdt_disable,value=true -nic user,model=open_eth,hostfwd=tcp::8081-:80 -s -S

# XXX: somehow generate output
#./qemu/xtensa-softmmu/qemu-system-xtensa -nographic -machine esp32 -drive file=./example_esp32_server/tcp_server.img,if=mtd,format=raw -s -S

# tasmota tests: don't fully boot
#./qemu/xtensa-softmmu/qemu-system-xtensa -nographic -machine esp32 -drive file=./example_esp32_server/tasmota.img,if=mtd,format=raw -global driver=timer.esp32.timg,property=wdt_disable,value=true -nic user,model=open_eth,hostfwd=tcp::8081-:80  \
#../qemu-new/build/xtensa-softmmu/qemu-system-xtensa -nographic -machine esp32 -drive file=./example_esp32_server/tasmota.img,if=mtd,format=raw -global driver=timer.esp32.timg,property=wdt_disable,value=true -nic user,model=open_eth,hostfwd=tcp::8081-:80 \
#-s -S

# XXX: fuzz inserts gabage and causes shit timer wirte null deref -- maybe off by one.
# XXX: fuzz data seems off. the restored state doesn't run.
# start and dump registers and memory
#./qemu/xtensa-softmmu/qemu-system-xtensa -nographic -machine esp32 -drive file=./example_esp32_server/tcp_server.img,if=mtd,format=raw -global driver=timer.esp32.timg,property=wdt_disable,value=true -nic user,model=open_eth,hostfwd=tcp::8081-:80 \
#-fuzz setup=$setup,entry=$entry,exit=$exit,len=a5,data=0x3ffbbc30,dump_file=./example_esp32_server/dump.bin,regs_file=./example_esp32_server/regs_dump.txt

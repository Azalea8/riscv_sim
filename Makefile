init:
	sudo apt-get update
	sudo apt-get install gcc-riscv64-unknown-elf

mini-rv32i:
	gcc -g -o mini-rv32i mini-rv32i.c

test-code:
	riscv64-unknown-elf-gcc -nostdlib -fno-builtin -march=rv32i -mabi=ilp32 -g -Wall test/test.c -o test/test.elf -T test/script.ld
	riscv64-unknown-elf-objcopy -O binary test/test.elf test/test.bin
	riscv64-unknown-elf-objdump -D -b binary -m riscv:rv32 test/test.bin > testcode.txt

run:
	./mini-rv32i test/test.bin 10 > output.txt

clean:
	rm -f mini-rv32i output.txt testcode.txt test/test.bin test/test.elf

.PHONY: clean

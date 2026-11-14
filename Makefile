NASM    ?= nasm
LD      ?= ld

SRC     := $(wildcard src/*.asm src/*.inc)

slip: build/slip.o
	$(LD) -o $@ $<

build/slip.o: $(SRC)
	@mkdir -p build
	$(NASM) -f elf64 -I src/ -o $@ src/slip.asm

test: slip
	./tests/run.sh

clean:
	rm -rf build slip

.PHONY: test clean

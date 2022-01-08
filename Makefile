SRCS := $(wildcard *.c)
OBJS := $(patsubst %c,%c.o,$(SRCS))
all: mkdir ${OBJS} link
mkdir:
	@mkdir -p build
%.c.o:%.c
	@echo -e "\e[32;1m[CC]\e[0m\t" $<
	@$(CC) ${CFLAGS} -g -c $< -o build/$@
link:
	@echo -e "\e[34;1m[CCLD]\e[0m	" fhhfs
	@$(CC) ${LDLFAGS} build/fhhfs.c.o -o build/fhhfs -lfuse -lz
	@echo -e "\e[34;1m[CCLD]\e[0m	" mkfs.fhhfs
	@$(CC) ${LDLFAGS} build/mkfs.fhhfs.c.o -o build/mkfs.fhhfs -lz
clean:
	@echo -e "\e[33;1m[CLEAN]\e[0m	" ALL
	@rm -rf build/*

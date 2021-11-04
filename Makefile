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
	@$(CC) ${LDLFAGS} build/*.o -o build/fhhfs -lfuse -lz
clean:
	@echo -e "\e[33;1m[CLEAN]\e[0m	" fhhfs
	@rm -rf build/*
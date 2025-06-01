BUILDDIR := build
TARGETS := $(addprefix $(BUILDDIR)/, fhhfs mkfs.fhhfs)

CFLAGS := -O2 -Wall -Werror
LDFLAGS := -lfuse -lz

.PHONY: all clean

all: $(BUILDDIR) $(TARGETS)
$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(TARGETS): %: %.o
	@echo -e "\e[34;1m[CCLD]\e[0m\t" $<
	@$(CC) $(LDFLAGS) $< -o $@

$(BUILDDIR)/%.o: %.c
	@echo -e "\e[32;1m[CC]\e[0m\t" $<
	@$(CC) $(CFLAGS) -c $< -o $@
	
clean:
	@echo -e "\e[33;1m[CLEAN]\e[0m\t" ALL
	@rm -rf build

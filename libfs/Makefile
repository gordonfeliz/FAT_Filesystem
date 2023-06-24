# Target library
lib := libfs.a

# Compiler/compiler flags -Wall -Werror -Wextra
CFLAGS := -Wall -Wextra
CC := gcc

all: $(lib)

# Files to compile
targets := disk.o fs.o

# Add dependencies
dependencies := $(targets:.o=.d)
-include $(dependencies)

# Compiles .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Builds library
$(lib): $(targets)
	ar rcs $@ $(targets)

# Cleans build files
clean:
	rm -f $(lib) $(targets) $(dependencies)
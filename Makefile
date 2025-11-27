# Makefile for libqgen
#
# Usage:
#	make			# Builds static library (libqgen.a)
#	make shared		# Builds shared library (libqgen.so or qgen.dll)
#	make clean		# Cleans up
#	make all		# Builds both
#	make install	# Copy build files to $(PREFIX)

CC ?= cc
CFLAGS ?= -O3 -Wall
SRC := qgen.c

PREFIX ?= /usr/bin

# --- Platform Detection ---
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
# Detect Windows if OS env is set or if uname reports MinGW/MSYS/Cygwin
IS_WINDOWS := $(if $(or $(filter Windows_NT,$(OS)),$(findstring MINGW,$(UNAME_S)),$(findstring MSYS,$(UNAME_S)),$(findstring CYGWIN,$(UNAME_S))),yes,)

# --- Configuration ---

# Static build artifacts
OBJ_STATIC := qgen.o
LIB_STATIC := libqgen.a
DEFS_STATIC := -DQGEN_BUILD

# Shared build artifacts (Compiled with PIC and SHARED defines)
OBJ_SHARED := qgen.sh.o
DEFS_SHARED := -DQGEN_BUILD -DQGEN_SHARED

ifeq ($(IS_WINDOWS),yes)
	LIB_SHARED := qgen.dll
	PICFLAG :=
	# Windows native cleanup: /Q (Quiet), /F (Force read-only)
	CLEAN_CMD := del /Q /F
	COPY_CMD := copy /B
else
	LIB_SHARED := libqgen.so
	PICFLAG := -fPIC
	# Unix native cleanup
	CLEAN_CMD := rm -f
	COPY_CMD := cp
endif

.PHONY: all static shared clean install install_shared install_static

# --- Rules ---

# Default target: check if SHARED=1 was passed, otherwise default to static
ifeq ($(SHARED),1)
all: shared
install: install_shared
else
all: static
install: install_static
endif

static: $(LIB_STATIC)
shared: $(LIB_SHARED)

# Link Static Library
$(LIB_STATIC): $(OBJ_STATIC)
	ar rcs $@ $^

# Link Shared Library
$(LIB_SHARED): $(OBJ_SHARED)
	$(CC) -shared -o $@ $^

# Compile Object for Static Lib
$(OBJ_STATIC): $(SRC)
	$(CC) $(CFLAGS) $(DEFS_STATIC) -c -o $@ $<

# Compile Object for Shared Lib (needs -fPIC and QGEN_SHARED)
$(OBJ_SHARED): $(SRC)
	$(CC) $(CFLAGS) $(PICFLAG) $(DEFS_SHARED) -c -o $@ $<

clean:
	-$(CLEAN_CMD) $(LIB_STATIC) $(LIB_SHARED) $(OBJ_STATIC) $(OBJ_SHARED) 2>NUL || true

install_static: $(PREFIX)
	-$(COPY_CMD) $(LIB_STATIC) "$(PREFIX)/$(LIB_STATIC)"

install_shared: $(PREFIX)
	-$(COPY_CMD) $(LIB_SHARED) "$(PREFIX)/$(LIB_SHARED)"

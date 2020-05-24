TARGET		:= busexmp
LIBOBJS 	:= buse.o io.o lfqueue.o heap.o
OBJS		:= $(TARGET:=.o) $(LIBOBJS)
STATIC_LIB	:= libbuse.a

CC		:= /usr/bin/gcc
override CFLAGS += -g -pedantic -Wall -Wextra -O2 -MMD -MP -fPIC -march=native -std=c99 -D_GNU_SOURCE -D_USE_GNU #-fsanitize=address
LDFLAGS		:= -L. -lbuse -lpthread -laio #-fsanitize=address

.PHONY: all clean test
all: $(TARGET)

$(TARGET): %: %.o $(STATIC_LIB)
	$(CC) -o $@ $< $(LDFLAGS)

$(TARGET:=.o): %.o: %.c buse.h
	$(CC) $(CFLAGS) -o $@ -c $<

$(STATIC_LIB): $(LIBOBJS)
	ar rcu $(STATIC_LIB) $(LIBOBJS)

$(LIBOBJS): %.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

test: $(TARGET)
	PATH=$(PWD):$$PATH sudo test/busexmp.sh
	PATH=$(PWD):$$PATH sudo test/signal_termination.sh

clean:
	rm -f $(TARGET) $(OBJS) $(STATIC_LIB)

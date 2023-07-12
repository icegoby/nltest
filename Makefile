CC ?= "gcc"
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common \
		  -Werror-implicit-function-declaration -Wsign-compare
CFLAGS += -I/usr/include/libnl3

LDFLAGS =

OBJS = nltest.o

ALL = nltest

LIBS = -lnl-3 -lnl-genl-3

all: $(ALL)

%.o: %.c
		$(CC) $(CFLAGS) -c -o $@ $<

nltest: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o nltest

clean:
	rm -f $(ALL) *.o

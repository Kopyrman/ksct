CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -Wpedantic -Wconversion -std=c17 -O2 -I /usr/X11R6/include
LDFLAGS ?= -L /usr/X11R6/lib -s
LIBS = -lX11 -lXrandr -lm

PREFIX ?= /usr
BIN ?= $(PREFIX)/bin
MAN ?= $(PREFIX)/share/man/man1

INSTALL ?= install

PROG = ksct
SRCS = src/ksct.c


$(PROG): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

install: $(PROG) $(PROG).1
	$(INSTALL) -d $(DESTDIR)$(BIN)
	$(INSTALL) -m 0755 $(PROG) $(DESTDIR)$(BIN)
	$(INSTALL) -d $(DESTDIR)$(MAN)
	$(INSTALL) -m 0644 $(PROG).1 $(DESTDIR)$(MAN)

uninstall:
	rm -f $(BIN)/$(PROG)
	rm -f $(MAN)/$(PROG).1

clean:
	rm -f $(PROG)

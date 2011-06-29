PREFIX=/usr
CC=gcc
CFLAGS=-Wall -fno-strict-aliasing -O
CXX=g++
CXXFLAGS=$(CFLAGS)

srcs = plugenv.cxx ecc_rs.c
objs = plugenv.o ecc_rs.o

all: plugenv

plugenv: $(objs)
	$(CXX) $(CXXFLAGS) -o $@ $^

plugenv.o: plugenv.cxx
	$(CXX) -c $(CXXFLAGS) -o $@ $<

clean:
	rm -f plugenv $(objs) .depend *~

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/sbin
	install -m755 -s plugenv -t $(DESTDIR)$(PREFIX)/sbin

.depend: *.[ch] *.cxx
	$(CC) -MM $(srcs) >.depend

-include .depend

.PHONY: clean all install


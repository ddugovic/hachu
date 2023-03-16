srcdir = .

CC?=gcc
CFLAGS?=-g -O2 -Wall -Wno-parentheses
#CFLAGS?=-O2 -s -Wall -Wno-parentheses

prefix=/usr/local
DATADIR=`xboard --show-config Datadir`

.PHONY: clean dist dist-clean install
ALL= hachu hachu.6.gz

all: ${ALL}

hachu: eval.o hachu.o move.o piece.o variant.o
	$(CC) $(CPPFLAGS) $(CFLAGS) eval.o hachu.o move.o piece.o variant.o $(LDFLAGS) -o hachu

%.o: %.c %.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

install: ${ALL} ${srcdir}/svg/*
	install -d -m0755 $(DESTDIR)$(prefix)/games
	cp -u ${srcdir}/hachu $(DESTDIR)$(prefix)/games
	install -d -m0755 $(DESTDIR)$(prefix)/share/man/man6
	cp -u ${srcdir}/hachu.6.gz $(DESTDIR)$(prefix)/share/man/man6
	install -d -m0755 $(DESTDIR)$(DATADIR)/themes/chu
	cp -u ${srcdir}/svg/*.svg $(DESTDIR)$(DATADIR)/themes/chu
	install -d -m0755 $(DESTDIR)$(DATADIR)/themes/conf
	cp -u ${srcdir}/svg/sho ${srcdir}/svg/chu $(DESTDIR)$(DATADIR)/themes/conf
	install -d -m0755 $(DESTDIR)/usr/share/games/plugins/logos
	cp -u ${srcdir}/logo.png $(DESTDIR)/usr/share/games/plugins/logos/hachu.png
	install -d -m0755 $(DESTDIR)/usr/share/games/plugins/xboard
	cp -u ${srcdir}/hachu.eng $(DESTDIR)/usr/share/games/plugins/xboard

hachu.6.gz: README.pod
	pod2man -s 6 README.pod | gzip -9n > hachu.6.gz

clean:
	rm -f ${ALL} *.o

dist-clean:
	rm -f hachu.tar.gz ${ALL} *~ chu/*~ md5sums

dist:
	install -d -m0755 HaChu
	install -d -m0755 HaChu/svg
	rm -f hachu.tar hachu.tar.gz
	cp *.c *.h README.pod Makefile hachu.eng logo.png HaChu
	cp svg/* HaChu/svg
	(md5sum HaChu/* HaChu/svg/* > HaChu/md5sums) || true
	tar -cvvf hachu.tar HaChu
	gzip hachu.tar
	rm HaChu/svg/*
	rmdir HaChu/svg
	rm HaChu/*
	rmdir HaChu

uninstall:
	rm -f $(DESTDIR)$(prefix)/share/man/man6/hachu.6.gz
	rm -f $(DESTDIR)$(prefix)/games/hachu
	rm -f $(DESTDIR)/usr/share/games/plugins/logos/hachu.png
	rm -f $(DESTDIR)/usr/share/games/plugins/xboard/hachu.eng


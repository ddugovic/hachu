srcdir = .

CC?=gcc
CFLAGS?= -O2 -s

DATADIR=`xboard --show-config Datadir`

ALL= hachu hachu.6.gz

all: ${ALL}

hachu: hachu.c
	$(CC) $(CFLAGS) $(LDFLAGS) hachu.c -o hachu

install: ${ALL} ${srcdir}/svg/*
	install -d -m0755 $(DESTDIR)/usr/games
	cp -u ${srcdir}/hachu $(DESTDIR)/usr/games
	install -d -m0755 $(DESTDIR)/usr/share/man/man6
	cp -u ${srcdir}/hachu.6.gz $(DESTDIR)/usr/share/man/man6
	install -d -m0755 $(DESTDIR)$(DATADIR)/themes/chu
	cp -u ${srcdir}/svg/*.svg $(DESTDIR)$(DATADIR)/themes/chu
	install -d -m0755 $(DESTDIR)$(DATADIR)/themes/conf
	cp -u ${srcdir}/svg/sho ${srcdir}/svg/chu $(DESTDIR)$(DATADIR)/themes/conf
	install -d -m0755 $(DESTDIR)/usr/share/games/plugins/logos
	cp -u ${srcdir}/logo.png $(DESTDIR)/usr/share/games/plugins/logos/hachu.png
	install -d -m0755 $(DESTDIR)/usr/share/games/plugins/xboard
	cp -u ${srcdir}/hachu.eng $(DESTDIR)/usr/share/games/plugins/xboard

hachu.6.gz: hachu.pod
	pod2man -s 6 hachu.pod > hachu.man
	cp hachu.man hachu.6
	rm -f hachu.6.gz
	gzip hachu.6

clean:
	rm -f ${ALL}

dist-clean:
	rm -f ${ALL} *~ chu/*~ *.man md5sums

dist:
	install -d -m0755 HaChu
	install -d -m0755 HaChu/svg
	rm -f hachu.tar hachu.tar.gz
	cp hachu.c hachu.pod Makefile hachu.eng logo.png HaChu
	cp chu/* HaChu/svg
	(md5sum HaChu/* HaChu/svg/* > HaChu/md5sums) || true
	tar -cvvf hachu.tar HaChu
	gzip hachu.tar
	rm HaChu/svg/*
	rmdir HaChu/svg
	rm HaChu/*
	rmdir HaChu

uninstall:
	rm -f $(DESTDIR)/usr/share/man/man6/hachu.6.gz
	rm -f $(DESTDIR)/usr/games/hachu
	rm -f $(DESTDIR)/usr/share/games/plugins/logos/hachu.png
	rm -f $(DESTDIR)/usr/share/games/plugins/xboard/hachu.eng


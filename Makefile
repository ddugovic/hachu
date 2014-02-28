srcdir = .

CC?=gcc
CFLAGS?= -O2 -s


ALL= hachu hachu.6.gz

all: ${ALL}

hachu: hachu.c
	$(CC) $(CFLAGS) hachu.c -o hachu

install: ${ALL} ${srcdir}/svg/*
	cp -u ${srcdir}/hachu $(DESTDIR)/usr/games
	install -d -m0755 $(DESTDIR)/usr/share/man/man6
	cp -u ${srcdir}/hachu.6.gz $(DESTDIR)/usr/share/man/man6
	install -d -m0755 `xboard --show-config Datadir`/themes/chu
	cp -u ${srcdir}/svg/*.svg `xboard --show-config Datadir`/themes/chu
	install -d -m0755 `xboard --show-config Datadir`/themes/conf
	cp -u ${srcdir}/svg/sho ${srcdir}/svg/chu `xboard --show-config Datadir`/themes/conf

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
	cp hachu.c hachu.pod Makefile HaChu
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


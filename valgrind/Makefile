VALGRIND_SRC = valgrind_src

top: $(VALGRIND_SRC)
	$(MAKE) -C $(VALGRIND_SRC)

VALGRIND_VERSION = valgrind-3.18.1
VALGRIND_TARBALL = $(VALGRIND_VERSION).tar.bz2

$(VALGRIND_TARBALL):
	wget https://sourceware.org/pub/valgrind/$(VALGRIND_TARBALL)

$(VALGRIND_SRC): $(VALGRIND_TARBALL)
	#svn co svn://svn.valgrind.org/valgrind/trunk $(VALGRIND_SRC)
	tar xjf $(VALGRIND_TARBALL)
	mv $(VALGRIND_VERSION) $(VALGRIND_SRC)
	cp -rf patch_memview.diff $(VALGRIND_SRC)
	cp -rf patch_wrapmalloc.diff $(VALGRIND_SRC)
	cp -rf patch_vex_floattypes.diff $(VALGRIND_SRC)/VEX
	cd $(VALGRIND_SRC); \
	    patch -p0 < patch_memview.diff; \
	    patch -p0 < patch_wrapmalloc.diff
	cd $(VALGRIND_SRC)/VEX; \
	    patch -p0 < patch_vex_floattypes.diff
	cp -rf memview $(VALGRIND_SRC)
	# Make links for mv_ipc.h and mv_main.c for simpler development
	rm -f $(VALGRIND_SRC)/memview/mv_ipc.h
	rm -f $(VALGRIND_SRC)/memview/mv_main.c
	cd $(VALGRIND_SRC)/memview; \
	    ln -s ../../../mv_ipc.h mv_ipc.h; \
	    ln -s ../../memview/mv_main.c mv_main.c
	cd $(VALGRIND_SRC); ./autogen.sh; ./configure

clean:
	rm -rf $(VALGRIND_SRC) $(VALGRIND_TARBALL)

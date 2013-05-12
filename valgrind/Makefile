VALGRIND_SRC = valgrind_src

top: $(VALGRIND_SRC)
	$(MAKE) -C $(VALGRIND_SRC)

# We currently require changes from the development baseline for the
# memview tool to work.  When the next valgrind version is released, it
# should be possible to patch the tarball directly.
ifdef USE_TARBALL

VALGRIND_VERSION = valgrind-3.8.1
VALGRIND_TARBALL = $(VALGRIND_VERSION).tar.bz2

$(VALGRIND_TARBALL):
	wget http://valgrind.org/downloads/$(VALGRIND_TARBALL)

$(VALGRIND_SRC): $(VALGRIND_TARBALL)
	tar xjf $(VALGRIND_TARBALL)
	mv $(VALGRIND_VERSION) $(VALGRIND_SRC)

else

$(VALGRIND_SRC):
	svn co svn://svn.valgrind.org/valgrind/trunk $(VALGRIND_SRC)
	cp -rf patch_memview.diff $(VALGRIND_SRC)
	cp -rf patch_wrapmalloc.diff $(VALGRIND_SRC)
	cp -rf patch_vex_floattypes.diff $(VALGRIND_SRC)/VEX
	cd $(VALGRIND_SRC); \
	    patch -p0 < patch_memview.diff; \
	    patch -p0 < patch_wrapmalloc.diff
	cd $(VALGRIND_SRC)/VEX; \
	    patch -p0 < patch_vex_floattypes.diff
	cp -rf memview $(VALGRIND_SRC)
	# Make symbolic links for mv_ipc.h and mv_main.c for simpler
	# development
	rm -f $(VALGRIND_SRC)/mv_ipc.h
	rm -f $(VALGRIND_SRC)/mv_main.c
	ln -s memview/mv_ipc.h $(VALGRIND_SRC)/mv_ipc.h
	ln -s memview/mv_main.c $(VALGRIND_SRC)/mv_main.c
	cd $(VALGRIND_SRC); ./autogen.sh; ./configure

endif

clean:
	rm -rf $(VALGRIND_SRC)

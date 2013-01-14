top:

patch_valgrind:
	cp -rf memview $(VALGRIND_SRC)
	cp -rf patch_memview.diff $(VALGRIND_SRC)
	cp -rf patch_wrapmalloc.diff $(VALGRIND_SRC)
	cd $(VALGRIND_SRC); \
	    patch -p0 < patch_memview.diff; \
	    patch -p0 < patch_wrapmalloc.diff

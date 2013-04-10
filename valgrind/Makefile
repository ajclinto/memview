top:

patch:
	cp -rf memview $(VALGRIND_SRC)
	cp -rf patch_memview.diff $(VALGRIND_SRC)
	cp -rf patch_wrapmalloc.diff $(VALGRIND_SRC)
	cp -rf patch_vex_floattypes.diff $(VALGRIND_SRC)/VEX
	cd $(VALGRIND_SRC); \
	    patch -p0 < patch_memview.diff; \
	    patch -p0 < patch_wrapmalloc.diff
	cd $(VALGRIND_SRC)/VEX; \
	    patch -p0 < patch_vex_floattypes.diff

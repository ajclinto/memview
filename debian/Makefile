VERSION = 1.0-1

BIN_INSTALL_DIR = debian/usr/bin
SHARE_INSTALL_DIR = debian/usr/share/memview
VALGRIND_INSTALL_DIR = debian/usr/lib/valgrind

VALGRIND_SRC = ../valgrind/valgrind_src

# depends: dpkg-shlibdeps --ignore-missing-info -O debian/usr/bin/memview

PACKAGE_NAME = memview_$(VERSION)_amd64.deb

MEMVIEW_BIN = ../memview
MEMVIEW_FRAG = ../shader.frag
MEMVIEW_VERT = ../shader.vert
MEMVIEW_TOOL = $(VALGRIND_SRC)/memview/memview-amd64-linux
MEMVIEW_PRELOAD = $(VALGRIND_SRC)/memview/vgpreload_memview-amd64-linux.so

top: $(PACKAGE_NAME)

control: control.src
	sed -e "s/<<architecture>>/`dpkg --print-architecture`/g" \
	    -e "s/<<version>>/$(VERSION)/g" control.src > control

$(PACKAGE_NAME): control $(MEMVIEW_BIN) $(MEMVIEW_FRAG) $(MEMVIEW_VERT) $(MEMVIEW_TOOL) $(MEMVIEW_PRELOAD)
	mkdir -p $(BIN_INSTALL_DIR)
	cp $(MEMVIEW_BIN) $(BIN_INSTALL_DIR)
	mkdir -p $(SHARE_INSTALL_DIR)
	cp $(MEMVIEW_FRAG) $(SHARE_INSTALL_DIR)
	cp $(MEMVIEW_VERT) $(SHARE_INSTALL_DIR)
	mkdir -p $(VALGRIND_INSTALL_DIR)
	cp $(MEMVIEW_TOOL) $(VALGRIND_INSTALL_DIR)
	cp $(MEMVIEW_PRELOAD) $(VALGRIND_INSTALL_DIR)
	mkdir -p debian/DEBIAN
	cp control debian/DEBIAN
	dpkg-deb --build debian
	mv debian.deb $(PACKAGE_NAME)

clean:
	rm -rf debian
	rm -f control
	rm -f $(PACKAGE_NAME)

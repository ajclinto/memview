## Summary

Memview is a real-time visualization program that will show the memory
state of another running program as a graphical image.  Memory addresses
correspond to pixels in the image, and as memory is accessed the display
will animate to show what parts of the address space are currently being
referenced by the program.

It was written primarily to satisfy my curiosity about how different
programs use their address space, and to serve as a visual debugger to help
find memory performance problems relating to locality.  It can also be
plain fun to watch the memory of a complex algorithm!

## Building

Before you start, you'll need:
* Qt4: libqt4-dev libqt4-opengl-dev
* A GPU with OpenGL 3.0 support

Build the visualizer front end:

    qmake
    make -j<nprocs>

To build the valgrind tool, check out the latest valgrind baseline, install
the patches in the valgrind/ director, and then build valgrind.  Here,
`<valgrind-dir>` is the location of the valgrind repository.

    svn co svn://svn.valgrind.org/valgrind/trunk <valgrind-dir>
    cd valgrind
    make patch_valgrind VALGRIND_SRC=<valgrind-dir>
    cd <valgrind-dir>
    ./autogen.sh
    ./configure --prefix=<valgrind-install-dir>
    make -j<nprocs> install

## Execution

Test the tool:

    valgrind --tool=memview ls

Test the front end:

    ./memview ls

If you were unable to patch the valgrind tool but you have a valgrind
binary installed, you can try the front end (without the tool) using:

    ./memview --tool=lackey ls

Lackey is orders of magnitude slower than the memview tool, and doesn't
support stack traces and allocation tracking - but you can get an idea of
how the memory trace visualization works.

## Details

Different colors indicate different types of memory accesses:
* Bright green   : Recently read
* Dark blue      : Previously read
* Bright yellow	 : Recently written
* Dark red       : Previously written
* Bright pink    : Recent instruction read
* Dark purple    : Previous instruction read
* Gray           : Allocated, unreferenced memory
* Dimmed         : Freed memory

Brighter colors indicate more recent references.  You can point at a pixel
and look at the status bar to see the address corresponding to that pixel,
as well as the type of the most recent memory operation.

Navigating the address space uses controls similar to google maps:
* Left click to pan
* Mouse wheel to zoom (each step is a factor of 2)
* Scroll bars to scroll

By default, the linear address space is converted to a 2D image using a
hilbert curve.  This kind of curve tends to map nearby memory addresses to
nearby pixels - so that locality of reference is immediately apparent.
It's also possible to map the memory in other ways using the options in the
'Visualization' menu.

Often there are large holes in the address space (for example, between the
heap and the stack) - these are automatically collapsed to eliminate empty
space in the image.  This means that if the program eventually makes use of
memory in one of these holes, the display will shift the existing data to
accomodate the newly visible addresses.

When using the memview tool, stack traces are periodically sampled and sent
to the visualizer.  These can be inspected by hovering the cursor over the
image - the closest stack trace will be displayed as a tooltip.

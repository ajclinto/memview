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
* Tested compilers are gcc 4.4 and 4.7

Build the visualizer front end:

    qmake
    make -j<nprocs>

To build the valgrind tool, check out the latest valgrind baseline, install
the patches in the valgrind/ director, and then build valgrind.  Here,
`<valgrind-dir>` is the location of the valgrind repository.

    svn co svn://svn.valgrind.org/valgrind/trunk <valgrind-dir>
    cd valgrind
    make patch VALGRIND_SRC=<valgrind-dir>
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

## Documentation

### Layout

Memview maps the program address space to pixels in the image.  At the
initial zoom level of 1, each pixel in the image corresponds to 4 bytes of
memory.

By default, the 1D address space is converted to a 2D image using a hilbert
curve.  This kind of mapping tends to map nearby memory addresses to nearby
pixels - so that locality of reference is immediately apparent.  It's
possible to layout the memory in other ways using the options in the
'Layout' menu.

To navigate the address space, use controls similar to Google maps:
* Left click/drag to pan
* Mouse wheel to zoom (each step is a factor of 2)
* Scroll bars to scroll

Often there are large holes in the address space (for example, between the
heap and the stack) - these are automatically collapsed to eliminate empty
space in the image.  This means that if the program eventually makes use of
memory in one of these holes, the display will shift the existing data to
accomodate the newly visible addresses.  If you want to see the entire
address space without any collapsing, change the layout from 'Compact' to
'Full Size' in the 'Layout' menu.

### Display

In the default 'Read/Write' display mode, different colors indicate different
types of memory accesses:
* Bright green   : Recently read
* Dark blue      : Previously read
* Bright yellow	 : Recently written
* Dark red       : Previously written
* Bright pink    : Recent instruction read
* Dark purple    : Previous instruction read
* Gray           : Allocated, unreferenced memory
* Dimmed         : Freed memory

Brighter colors indicate more recent references.  You can point at a pixel
and look at the status bar to see the address and mapping information
corresponding to that pixel, as well as the type for the most recent memory
operation at that address.

The 'Thread Ids' display mode will color the memory based on the thread
that most recently touched that memory.

The 'Data Type' display mode will color the memory based on the data type
that valgrind reported.  Be aware that the data type may not be accurate,
since machine instructions that access memory do not always specify the
data type.

The 'Mapped Regions' display mode will show the intervals in memory that
correspond to the different memory mappings in the program.  Each different
mapped region will display using a different color.  If you want to see the
full extent of the mapped regions, change the layout to 'Full Size' -
otherwise the mapped regions will only appear where memory has been
referenced.

During execution, stack traces are periodically sampled and sent to the
visualizer.  These can be inspected by hovering the cursor over the image -
the closest stack trace will be displayed as a tooltip.  To view the
location of the stacks that have been recorded, use the 'Stack Traces'
display mode.

### Data Type

If you zoom in far enough, memview will populate the zoomed in pixels with
the actual data that is present in those memory locations.  By default it
will display the values based on the type reported by valgrind, but you can
override the data display type using the settings in the 'Data Type' menu.


# PlaneGCS (from FreeCAD)

FreeCAD's 2D geometric constraint solver (the Sketcher's `planegcs`),
LGPL-2.1-or-later (see `LICENSE`). Taken from
[spookylukey/planegcs](https://github.com/spookylukey/planegcs) at commit
`cc39ad1f383ee04f296703fda1760d4a9a8ee3c3`, which extracts it from FreeCAD with
small shims (`shims/`) for FreeCAD's headers.

Changes made for OpenRevers (all marked `OpenRevers:` in the code):

- `GCS.cpp`: the split into independent subsystems uses a union-find instead
  of `boost::graph` (`adjacency_list`, `connected_components`); the Boost
  includes are gone.
- `Constraints.cpp`, `Geo.h`: unused Boost includes removed.
- `shims/boost_graph_adjacency_list.hpp`: removed.

OpenRevers links it statically. Under the LGPL, anyone may replace it with a
modified version: the sources here, OpenRevers' own sources (GPL-3.0) and its
build files are enough to rebuild the program with a changed PlaneGCS.

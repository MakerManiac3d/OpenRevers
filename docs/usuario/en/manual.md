# OpenRevers manual (version 0.2)

OpenRevers prepares scanned meshes for reverse engineering. This version
covers **phase 1**: open the scan, see what is wrong with it, repair it and
make it ready to model or print.

## The window

| Area | What for |
| --- | --- |
| Toolbar | The most used actions: save, import, export, undo, and the mesh operations |
| **Model** (left) | The project's bodies. The checkbox shows or hides one; double-click to rename |
| **Properties** | The selected body: triangles, vertices, size, area, source file |
| 3D view (centre) | The part. **Open edges** are drawn red; edges with three or more faces magenta |
| **History** (right) | Every operation done; greyed, those undone that can be redone |
| **Log** (bottom) | What each operation did, yours or the AI agent's, with its numbers |
| Status bar | The running operation and the AI agent's state |

Panels can be moved, closed and brought back from the **View** menu.

### Mouse and keyboard

| Action | How |
| --- | --- |
| Orbit | Left button drag |
| Pan | Middle or right button drag (or Shift + left) |
| Zoom | Wheel |
| Fit all | Double-click, or **F** |
| Standard views | **View** menu: isometric, front, back, left, right, top, bottom |
| Undo / redo | Ctrl+Z / Ctrl+Y |

Z is up, as in most CAD programs.

## Recommended workflow

1. **Import.** *File → Import mesh* (Ctrl+I), or drop the file on the window.
   STL (binary and text), OBJ and PLY, read as millimetres; XYZ and PTS
   point clouds; 3MF, converted
   from the unit it states, with every object of its build.
2. **Analyse.** *Mesh → Analyse mesh*. The report gives:
   - **holes** and their length. A one-sided scan has one long outer rim;
     small loops are holes;
   - **non-manifold edges**: edges shared by three or more faces. Repair
     them before anything else;
   - **orientation conflicts**: faces wound against their neighbours;
   - **pieces**: more than one is usually scanner debris;
   - **mean edge length**: the scan's resolution.
3. **Repair.** *Mesh → Repair mesh*, in one step:
   - welds coincident vertices (or those closer than the tolerance);
   - removes degenerate and duplicate faces;
   - removes small loose pieces;
   - orients every face consistently and outward.
4. **Fill holes.** *Mesh → Fill holes* closes holes whose rim has at most
   *maxEdges* edges, with a smooth patch that blends into the surface. Set
   *maxEdges* so the outer rim of an open scan stays open.
5. **Improve the mesh**, depending on what it is for:
   - **Smooth** reduces noise but also rounds sharp edges; use little.
   - **Remesh** makes even triangles: *uniform* at one edge length,
     *adaptive* finer where the surface curves. Edges sharper than
     *featureAngle* (30° by default) stay sharp.
   - **Decimate** makes a lighter mesh, with *maxError* as the deviation
     limit.
   - **Quad remesh** rebuilds the surface as quads that follow its shape and
     sharp edges, for subdivision or T-Spline modelling. *targetFaces* is the
     approximate number of quads. *View → Mesh edges* shows them. Any later
     operation on the mesh (repair, smooth, remesh…) turns it back into
     triangles.
6. **Save or export.** *File → Save project* (`.orvp`, every body), or
   *Export mesh* to STL, OBJ, PLY or 3MF. A quad mesh is written as quads in OBJ
   and PLY.
7. **Take it to Fusion as a T-Spline** (optional). With a **closed** quad
   mesh, *File → Export T-Spline for Fusion (.tsm)*. In Fusion, open the file
   as a Form body from a script (`TSplineBodies.addByTSMFile`). If the mesh is
   open, fill its holes before quad remeshing: the program says what stops the
   export.

### Starting from a point cloud

An XYZ or PTS file, or a PLY or OBJ without faces, comes in as a **point
cloud** (drawn as points). Mesh operations do not apply to a cloud; first
rebuild the surface from the *Points* menu:

1. **Thin points** (optional): one point per cube of side *spacing*. An even,
   lighter cloud reconstructs better and faster.
2. **Estimate normals**: each point gets the normal of the plane through its
   neighbours. *propagate* orients the whole cloud consistently (open or
   closed scans); *outward*, away from the centre (closed, roughly convex
   parts); *keep* keeps the side of the scanner's own normals.
3. **Mesh from points (Poisson)**: makes a new body with a closed mesh; the
   cloud is kept. *depth* sets the detail (8 is usually enough; each level
   more makes four times the triangles). On an open scan, Poisson closes
   what was not scanned: *trim* (0.05–0.1) removes it.

The mesh then follows the usual workflow from step 2.

Try an operation on a copy first (*Body → Duplicate body*) and compare.

## Settings

*File → Settings*:

- **Language**: the system's, Spanish or English; used from the next start.
- **AI agent**: whether an agent may connect, the local port, and the folders
  it may read and write. Your home folder if none is given.

## AI agent

See [docs/mcp/README.md](../../mcp/README.md). In short:

1. *AI agent → Copy configuration for Claude Desktop*.
2. Paste it into Claude Desktop's configuration and restart Claude Desktop.
3. Ask for what you need.

The agent uses the same commands you do, every step appears in the log and
the history, and every step can be undone.

## Command line

`openrevers-cli` sits beside the program:

```text
openrevers-cli run mesh.import '{"path":"part.stl"}' mesh.repair '{"body":"b1"}' mesh.fill_holes '{"body":"b1"}' mesh.export '{"body":"b1","path":"clean.stl"}'
openrevers-cli list        # every command and its inputs
openrevers-cli mcp         # MCP server for an agent
```

## Common problems

| Problem | Fix |
| --- | --- |
| "The file has no points or triangles" | The file is empty or not the format its extension says |
| "… is a point cloud; reconstruct a mesh from it first" | Use the *Points* menu: estimate normals, then mesh from points |
| The Poisson mesh has a "lid" where there were no points | Run it again with *trim* 0.05–0.1 |
| Holes are left open | They are larger than *maxEdges*: raise it, or use 0 for all |
| Remeshing rounds an edge | Lower *featureAngle* so the edge counts as sharp |
| "The quad mesh cannot be a T-Spline: it is open" | Fill the holes (maxEdges 0) and quad remesh again |
| "AI agent: port … busy" | Another program uses the port; change it in *Settings → AI agent* |

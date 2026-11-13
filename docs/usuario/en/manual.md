# OpenRevers manual (version 0.9)

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

## Working by hand: command panel, palette and results

Everything the AI agent does, you can do with the same commands and the same
data, without typing JSON:

- **Command panel** (on the right). It opens from any menu or from the
  palette and does not block the window: you can turn the view and click the
  part while it is open. *Apply* runs the command and leaves the panel open
  to repeat with other values.
- **Taking data from the view:** each field that names a place has its
  button:
  - **⌖** beside a point takes the point clicked;
  - beside a direction, the normal of the face clicked;
  - beside a body, the body clicked;
  - **From a face in the view** gives a plane;
  - **Add clicks** adds points in order (rounding edges, pairs of points);
  - **Take the lasso selection** passes the selected faces.
- **Lists instead of references:**
  - sketches, operations, surfaces, shapes and parameters come in drop-downs
    of the chosen part;
  - a loft's sketches are ticked and dragged into order;
  - the bodies to join are ticked;
  - a sketch's loops are drawn with **Draw in the sketch editor…**.
- **See it live** (panel checkbox): while you change the values, the result
  is shown in amber over the original body, which turns see-through; a
  section is drawn in magenta. Nothing changes or enters the history until
  *Apply*.
- **Handles in the view:** the panel's points (a sphere's centre, a box's
  corners, a plane's origin) show as orange dots you drag with the mouse; the
  box, sphere and plane are drawn as guides.
- **Find a command** (Ctrl+K): type what you want to do («section», «fill»,
  «align»), and Enter opens the panel.
- **Results** (tab beside the log): every command, yours or the agent's, with
  its whole input and answer (deviations, overlaps, residuals…), to look at
  later.

## Recommended workflow

1. **Import.** *File → Import mesh* (Ctrl+I), or drop the file on the window.
   STL (binary and text), OBJ and PLY, read as millimetres; E57 point clouds
   (every scan, placed by its pose and converted from metres to
   millimetres), XYZ and PTS point clouds; 3MF, converted
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

An E57, XYZ or PTS file, or a PLY or OBJ without faces, comes in as a **point
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

## Shapes and alignment

For mechanical parts (flat faces, holes, shafts, cones):

1. **Shapes → Find shapes.** The mesh is split into regions and each gets a
   plane, sphere, cylinder, cone or torus. The view colours them:
   - blue: planes;
   - green: cylinders;
   - orange: cones;
   - violet: spheres;
   - yellow: tori;
   - grey: what is no shape (fillets, freeform areas).

   The *Shapes* panel lists each with its size (Ø, angle, area) and its
   **σ**, the standard deviation of the mesh from the shape. A σ close to
   the scanner noise, which is also shown, means a true shape.
   - *noise*: the scanner noise, if you know it (0 to estimate it).
   - *tolerance*: how many times the noise a face may stand off the shape
     and still belong to it.
   - *angle*: largest normal deviation.
2. **Shapes → Fit a shape here**, for a hole or a face that was missed.
   Give a point on its surface and a radius.
3. **Shapes → Refit shape as…**, when a region came out as another type (a
   short cylinder taken for a plane, for example).
4. **Shapes → Measure between shapes**: angle and distance between two
   shapes, for example:
   - the thickness between two parallel faces;
   - the distance between two holes;
   - the distance from an axis to a face;
   - the point where an axis meets a face.
5. **Align** puts the part at the origin:
   - **Align by datums (3-2-1):** as on a measuring machine.
     - **Primary:** a large face or an axis. The part sits on that face and Z
       points into it.
     - **Secondary:** a side face or an axis; it sets X.
     - **Tertiary:** another face or a hole; it sets the rest of the origin.

     With a face and two holes, the origin is on the first hole and X points
     to the second.
   - **Align to principal axes:** quick; centres the part with its longest
     side on X.
   - **Best-fit alignment:** brings a scan onto another body (another scan
     or the nominal mesh) and reports the deviation left. If they are far
     apart or turned, set *coarse*.
   - **Move or rotate:** by hand.

   The shapes move with the part.

**With the mouse:**
- A **click** on the part marks the point (an orange cross) and picks the
  shape under it, which turns amber. The status bar shows the coordinates,
  the shape and its σ.
- **Ctrl+click** adds another shape. Order matters: in *Align by datums* the
  first picked is the primary, the second the secondary and the third the
  tertiary.
- A click on the background clears it all.
- A **right click**, without dragging, opens a menu of what can be done
  there: fit a shape at that point, refit, measure between the two picked
  shapes, align, rename or delete. The forms come filled in with the point
  and the picked shapes.
- Dragging is unchanged: left to orbit, right or middle to pan, and the
  wheel to zoom.
- The AI agent can read what you picked, so you can ask it things like
  "measure the distance between these two faces".

## Parametric part (phase 3)

A **part** is an exact solid with its history, exported as STEP for Fusion,
FreeCAD or SolidWorks. The *Model* panel shows its parameters, sketches and
operations under it. The AI can make it all, and you can touch up any part of
it without leaving the program.

1. **Part → New part.**
2. **Part → Section with a plane**, on the aligned scan: the part's outline
   at that height, in the plane's coordinates, to draw the sketch from.
3. **Part → Sketch from a section:** cuts the scan and turns each closed
   outline into lines and arcs (0.05 mm tolerance by default); holes come out
   as circles, corners sharp where lines cross, arcs tangent to the lines they
   touch. It gives every segment's size, to check against the calipers. A
   loop inside another is a hole.
4. **Part → Extrude** (join, cut, intersect), **Revolve** and **Fillet
   edges**. Values may be numbers or **parameter** names (*Part → Set a
   parameter*): change a parameter and the part is rebuilt. A change that
   breaks it is not applied, and the reason is given.
5. **Part → Compare with the scan:** the scan's distances to the solid, the
   median, 95th percentile, the share within a tolerance (±0.2 mm by default)
   and **the zones outside it** (where, how much of the scan, and to which
   side). The scan is **coloured** by its distance to the part, with a legend
   on the right: green within the tolerance, yellow to red where the model
   lacks material, cyan to blue where it has too much. *Body → Remove
   deviation colours* gives it its own colour back.
6. **File → Export STEP.**

A part's triangles only show it: the mesh tools do not change it.

### Free-form shapes (phase 4)

- **Organic outlines:** in *Sketch from a section*, tick *splines*: the
  outline's smooth runs (which otherwise come out as many short arcs) become
  splines within the tolerance. Sharp corners, long lines and clear arcs
  stay.
- **Loft through sections** (*Part → Loft through sections*): a solid through
  several sketches in order. For a blade, draw or take from the scan its
  section at several radii (each one closed loop starting at the leading
  edge, the planes facing the same way). *Ruled* makes it flat between
  sections.
- **Circular pattern** (*Part → Circular pattern*): copies of what another
  operation made (a blade, a hole) round an axis. The count and the angle may
  be parameters.
- **Surface from the scan** (*Part → Surface from the scan*): a smooth
  free-form surface fitted to a zone of the scan, a segmented shape or the
  points round a point. It gives its rms and worst deviation. It shows as a
  violet grid in the view and, with its figures, in the *Model* panel. Its
  context menu offers:
  - **Cut the part by it** (keeping what is below or above): extrude a block
    past the curved face, and the cut gives it the scan's shape;
  - **Thicken** into a solid: a shell or a panel.
- **In the sketch editor,** *To spline* turns a line or an arc into a spline,
  bent by dragging its poles (the circles joined by a dashed line).

A surface is seen from a plane: one that folds over itself seen from it is
made by sections and a loft instead.

### Touching up the part by hand

- **In the 3D view** a part shows its edges and its sketches (blue; the one
  picked, orange; *View → Sketches of parts* hides them). A click on the part
  picks the **face** under it, or the **edge** if the click is within a few
  pixels of one; Ctrl+click adds or takes away. The status bar gives its kind
  and size (plane and area, cylinder and diameter, line and length…).
- **Right-click on the part:** *New sketch on this face* (on a flat face),
  *Round the picked edges*, *Compare with a scan* and *See through* (the part
  half transparent, to see the scan inside; also on the scan).
- **In the *Model* panel,** pick a parameter, a sketch or an operation: its
  values appear in *Properties*, and double-click changes them (a number or a
  parameter's name); the mode with its drop-down, *Suppressed* with its box.
  Right-click an operation to edit its sketch, suppress it, **move it up or
  down** the history or delete it. An operation that fails shows in red.
- **Sketch editor** (double-click a sketch, or *Part → Edit the picked
  sketch*): the sketch over the **section of the scan** in its plane (grey
  dotted line). Each segment is coloured by how far it lies from the section:
  green within the tolerance, orange up to four times, red more.
  - *Select:* drag vertices, the diamond in the middle of an arc (changes its
    bulge), a circle's centre or the diamond of its radius. Double-click a
    line to make it an arc, an arc to make it a line.
  - *Rectangle* (two corners), *Circle* (centre and a point) and *Polygon*
    (click each corner; double-click or Enter closes it) draw new loops.
  - *Line ↔ arc*, *Split*, *Delete vertex* (Del), *Delete loop*, *Undo*,
    *Redo* and *Fit* (F).
  - On the right, the **values** of what is picked (vertex X/Y, a line's
    length, an arc's radius, a circle's centre and diameter), to type the
    exact size from the calipers.
  - It snaps to vertices and to the section while you drag (*Snap…* box).
    Wheel to zoom; middle or right button to pan.
  - *OK* changes the sketch and rebuilds the part in one undo step. A sketch
    that does not close cannot be accepted.

Everything you do by hand is visible to the AI agent (`view_selection` tells
it which parameter, sketch or operation, which faces and which edges you
picked), so you can point at something and ask it to carry on from there.

## Cleaning and aligning scans

**Cleaning** (*Mesh* menu; works on meshes and point clouds):

- **Remove the table:** finds the plane with most points (the table or
  turntable) and removes a band of `distance` on each side and, with
  *beyond*, everything below it. The plane can also be given (a point and
  its normal towards the part).
- **Remove stray points:** removes points far from their neighbours (noise,
  reflections). A lower *ratio* removes more.
- **Crop:** keeps what lies inside (or outside) a sphere, a box or a
  half-space.
- **Select with a lasso** (key L): drag with the left button round what to
  remove; it turns red.
  - Ctrl adds to the selection; Alt+drag turns the view.
  - Only faces facing you are taken, so what is behind is not touched.
  - **Delete** removes it (and can be undone).
- **Remove loose pieces** (already there) for the islands left afterwards.

**Aligning several scans** (*Align* menu):

- **Automatic alignment:** puts one scan onto another from their shape
  alone, however far apart and turned. They must overlap (a third or more).
  Gives the overlap and the deviation left.
- **Align by pairs of points:** a window opens beside the view.
  1. Click the same detail (a corner, a hole, a mark) on the scan to move
     (orange marks) and on the reference (cyan marks), in the same order.
  2. With three pairs or more, *Align*. Clicking near is enough: the best
     fit finishes it.
- **Best-fit alignment** (already there) to refine two scans already close.
- **Join scans:** puts aligned scans into one new body and hides the
  originals. With *asPoints*, as a point cloud to rebuild one mesh with
  *Points → Mesh from points*.

## Scanning from OpenRevers (phase 6, experimental)

> **Hidden by default since 0.10.5.** With the Raptor it connects and
> previews, but does not record the scan yet and may close the program. Scan
> in CrealityScan and import the file (OBJ, STL or PLY) instead. To try it:
> *Settings → Experimental → Direct scanning*, then restart OpenRevers.

With **CrealityScan installed** (Windows) and the scanner plugged in by USB
(Raptor, Otter, Ferret…), the **Scan** menu drives the scanner without
leaving OpenRevers:

1. **Scanner status:** whether CrealityScan's scanning library was found.
   OpenRevers does not ship it; it uses the one you installed
   (`C:\Program Files\CrealityScan\…\Plugins\x86_64`, or the folder in
   the `ORV_CREALITYSCAN_DIR` variable).
2. **Connect scanner:** what you scan (small, medium or big object, face,
   person), accuracy, surface (standard, black, white, reflective) and light
   (infrared or blue laser).
3. **Preview**, to aim, then **Start scanning**. The points show in amber in
   the view as they come. **Pause scanning** stops adding points and
   **Start scanning** goes on.
4. **Stop scanning** and **Scan to body:** the points (with normals) or the
   scanner's own mesh go into the project as a new body, in millimetres. Work
   on it like any imported scan.
5. **Disconnect scanner** when done.

Close CrealityScan before connecting: only one program can use the scanner
at a time. To try it without a scanner, **Use the scan simulator** gives a
test plate.

This is new: until tried on a real scanner, some details (the device's name,
the units) may come out wrong. If something fails, the log says what the
library returned.

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

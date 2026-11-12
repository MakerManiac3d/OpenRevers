// SPDX-License-Identifier: GPL-3.0-or-later
// The only file that talks to Open CASCADE. Written against the API common to
// 7.6 (Ubuntu 24.04) and 7.9 (the Windows build).
#include "core/solid/Solid.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeSegment.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace orv {

struct Solid::Impl {
    TopoDS_Shape shape;
};

namespace {

constexpr double kPi = 3.14159265358979323846;

gp_Pnt pnt(const Vec3d& v) { return {v.x(), v.y(), v.z()}; }
gp_Dir dir(const Vec3d& v) { return {v.x(), v.y(), v.z()}; }

Solid make(TopoDS_Shape s) {
    auto impl = std::make_shared<Solid::Impl>();
    impl->shape = std::move(s);
    return Solid(impl);
}

const TopoDS_Shape& shapeOf(const Solid& s) {
    static const TopoDS_Shape none;
    return s.impl() ? s.impl()->shape : none;
}

/// Runs an Open CASCADE call, turning its exceptions into SolidError.
template <class F>
auto guarded(const char* what, F&& f) -> decltype(f()) {
    try {
        return f();
    } catch (const Standard_Failure& e) {
        throw SolidError(std::string(what) + ": " + (e.GetMessageString() ? e.GetMessageString() : "Open CASCADE error"));
    }
}

/// Merges faces that lie on the same surface after a boolean (a hole drilled
/// through two coplanar pieces leaves one face, not two).
TopoDS_Shape unify(const TopoDS_Shape& s) {
    ShapeUpgrade_UnifySameDomain u(s, Standard_True, Standard_True, Standard_False);
    u.Build();
    return u.Shape();
}

/// The point halfway along an arc from a to b round its centre, in its sense.
Vec2d arcMiddle(const Vec2d& a, const Vec2d& b, const Segment& s) {
    const Vec2d u = a - s.centre, v = b - s.centre;
    const double a0 = std::atan2(u.y(), u.x());
    double sweep = std::atan2(v.y(), v.x()) - a0;
    if (s.ccw) {
        while (sweep <= 0) sweep += 2 * kPi;
    } else {
        while (sweep >= 0) sweep -= 2 * kPi;
    }
    const double r = 0.5 * (u.norm() + v.norm());
    return s.centre + r * Vec2d(std::cos(a0 + 0.5 * sweep), std::sin(a0 + 0.5 * sweep));
}

TopoDS_Wire wireOf(const Loop& loop, const SketchPlane& pl) {
    BRepBuilderAPI_MakeWire mw;
    const auto& sg = loop.segments;
    const gp_Dir n = dir(pl.normal);
    for (std::size_t i = 0; i < sg.size(); ++i) {
        const Segment& s = sg[i];
        if (s.kind == Segment::Kind::Circle) {
            const gp_Circ c(gp_Ax2(pnt(pl.toWorld(s.centre)), n, dir(pl.xDir)), s.radius);
            mw.Add(BRepBuilderAPI_MakeEdge(c).Edge());
            continue;
        }
        // Each segment starts exactly where the previous one ended, so the
        // edges share their vertices.
        const Vec2d a = i == 0 ? sg.back().b : sg[i - 1].b;
        const Vec2d b = s.b;
        const gp_Pnt pa = pnt(pl.toWorld(a)), pb = pnt(pl.toWorld(b));
        if (s.kind == Segment::Kind::Line) {
            mw.Add(BRepBuilderAPI_MakeEdge(GC_MakeSegment(pa, pb).Value()).Edge());
        } else {
            // Through its two ends and its middle: the ends are exactly the
            // neighbours' (an arc made on its circle would project them, and a
            // gap of a micron is enough for the wire to break).
            GC_MakeArcOfCircle arc(pa, pnt(pl.toWorld(arcMiddle(a, b, s))), pb);
            if (!arc.IsDone()) throw SolidError("an arc could not be made (its ends are not on its circle)");
            mw.Add(BRepBuilderAPI_MakeEdge(arc.Value()).Edge());
        }
        if (!mw.IsDone()) throw SolidError("segment " + std::to_string(i + 1) + " does not join the previous one");
    }
    if (!mw.IsDone()) throw SolidError("the loop could not be made into a wire");
    TopoDS_Wire w = mw.Wire();
    if (loop.area() < 0) w.Reverse();  // counterclockwise, so the face looks along the normal
    return w;
}

/// The face of each loop, with its nesting depth (0: an outline, 1: a hole in
/// it, 2: an island in the hole...).
std::vector<std::pair<TopoDS_Face, int>> facesOf(const Profile& p) {
    SketchPlane pl = p.plane;
    pl.normalize();
    std::vector<std::pair<TopoDS_Face, int>> out;
    for (std::size_t i = 0; i < p.loops.size(); ++i) {
        const auto& L = p.loops[i];
        BRepBuilderAPI_MakeFace mf(gp_Pln(pnt(pl.origin), dir(pl.normal)), wireOf(L, pl), Standard_True);
        if (!mf.IsDone()) throw SolidError("loop " + std::to_string(i + 1) + " does not make a face");
        // A point of this loop decides which other loops it lies in.
        const auto poly = L.polygon(0.05);
        const Vec2d q = poly.empty() ? Vec2d::Zero() : poly.front();
        int depth = 0;
        for (std::size_t j = 0; j < p.loops.size(); ++j)
            if (j != i && std::abs(p.loops[j].area()) > std::abs(L.area()) && p.loops[j].contains(q, 0.01)) ++depth;
        out.emplace_back(mf.Face(), depth);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return out;
}

/// Material where the depth is even, holes where it is odd, applied from the
/// outside in.
template <class Sweep>
TopoDS_Shape fromFaces(const std::vector<std::pair<TopoDS_Face, int>>& faces, Sweep&& sweep) {
    TopoDS_Shape result;
    for (const auto& [face, depth] : faces) {
        TopoDS_Shape piece = sweep(face);
        if (result.IsNull()) {
            if (depth % 2) throw SolidError("a hole lies outside every outline");
            result = piece;
        } else if (depth % 2 == 0) {
            BRepAlgoAPI_Fuse op(result, piece);
            if (!op.IsDone()) throw SolidError("joining a loop failed");
            result = op.Shape();
        } else {
            BRepAlgoAPI_Cut op(result, piece);
            if (!op.IsDone()) throw SolidError("cutting a hole failed");
            result = op.Shape();
        }
    }
    return unify(result);
}

}  // namespace

Solid::Solid() = default;
Solid::~Solid() = default;
Solid::Solid(const Solid&) = default;
Solid& Solid::operator=(const Solid&) = default;
Solid::Solid(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}

bool Solid::empty() const { return !impl_ || impl_->shape.IsNull(); }

Solid Solid::extrude(const Profile& profile, double start, double end) {
    if (const auto why = profile.problem(); !why.empty()) throw SolidError(why);
    if (!(end > start)) throw SolidError("the extrusion needs end > start");
    return guarded("extrude", [&] {
        SketchPlane pl = profile.plane;
        pl.normalize();
        const auto faces = facesOf(profile);
        return make(fromFaces(faces, [&](const TopoDS_Face& f) {
            gp_Trsf t;
            t.SetTranslation(gp_Vec(dir(pl.normal)) * start);
            const TopoDS_Shape moved = BRepBuilderAPI_Transform(f, t, Standard_True).Shape();
            BRepPrimAPI_MakePrism prism(moved, gp_Vec(dir(pl.normal)) * (end - start));
            if (!prism.IsDone()) throw SolidError("the extrusion failed");
            return prism.Shape();
        }));
    });
}

Solid Solid::revolve(const Profile& profile, const Vec2d& axisPoint, const Vec2d& axisDir, double angleDeg) {
    if (const auto why = profile.problem(); !why.empty()) throw SolidError(why);
    if (axisDir.norm() < 1e-12) throw SolidError("the axis needs a direction");
    return guarded("revolve", [&] {
        SketchPlane pl = profile.plane;
        pl.normalize();
        const Vec3d o = pl.toWorld(axisPoint), d = (pl.toWorld(axisPoint + axisDir) - o).normalized();
        const gp_Ax1 ax(pnt(o), dir(d));
        const double ang = std::clamp(angleDeg, -360.0, 360.0) * kPi / 180;
        return make(fromFaces(facesOf(profile), [&](const TopoDS_Face& f) {
            BRepPrimAPI_MakeRevol rev(f, ax, ang);
            if (!rev.IsDone()) throw SolidError("the revolution failed (does the profile cross the axis?)");
            return rev.Shape();
        }));
    });
}

Solid Solid::fused(const Solid& o) const {
    return guarded("join", [&] {
        BRepAlgoAPI_Fuse op(shapeOf(*this), shapeOf(o));
        if (!op.IsDone()) throw SolidError("join failed");
        return make(unify(op.Shape()));
    });
}

Solid Solid::cut(const Solid& o) const {
    return guarded("cut", [&] {
        BRepAlgoAPI_Cut op(shapeOf(*this), shapeOf(o));
        if (!op.IsDone()) throw SolidError("cut failed");
        return make(unify(op.Shape()));
    });
}

Solid Solid::common(const Solid& o) const {
    return guarded("intersect", [&] {
        BRepAlgoAPI_Common op(shapeOf(*this), shapeOf(o));
        if (!op.IsDone()) throw SolidError("intersect failed");
        return make(unify(op.Shape()));
    });
}

Solid Solid::transformed(const Eigen::Isometry3d& t) const {
    return guarded("move", [&] {
        const Eigen::Matrix4d m = t.matrix();
        gp_Trsf tr;
        tr.SetValues(m(0, 0), m(0, 1), m(0, 2), m(0, 3), m(1, 0), m(1, 1), m(1, 2), m(1, 3), m(2, 0), m(2, 1), m(2, 2), m(2, 3));
        return make(BRepBuilderAPI_Transform(shapeOf(*this), tr, Standard_True).Shape());
    });
}

Solid Solid::filleted(const std::vector<Vec3d>& points, double r, double tol) const {
    if (r <= 0) throw SolidError("a fillet needs a radius");
    return guarded("fillet", [&] {
        BRepFilletAPI_MakeFillet f(shapeOf(*this));
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(shapeOf(*this), TopAbs_EDGE, edges);
        int added = 0;
        for (const auto& p : points) {
            const TopoDS_Shape v = BRepBuilderAPI_MakeVertex(pnt(p)).Shape();
            int best = 0;
            double bestD = tol;
            for (int i = 1; i <= edges.Extent(); ++i) {
                BRepExtrema_DistShapeShape d(v, edges(i));
                if (d.IsDone() && d.Value() < bestD) { bestD = d.Value(); best = i; }
            }
            if (!best) throw SolidError("no edge within " + std::to_string(tol) + " of a fillet point");
            f.Add(r, TopoDS::Edge(edges(best)));
            ++added;
        }
        if (!added) throw SolidError("a fillet needs at least one edge");
        f.Build();
        if (!f.IsDone()) throw SolidError("the fillet failed (radius too large for the faces around it?)");
        return make(f.Shape());
    });
}

Solid Solid::readStep(const std::filesystem::path& path) {
    return guarded("STEP", [&] {
        STEPControl_Reader r;
        if (r.ReadFile(path.string().c_str()) != IFSelect_RetDone) throw SolidError("could not read " + path.filename().string() + " as STEP");
        r.TransferRoots();
        TopoDS_Shape s = r.OneShape();
        if (s.IsNull()) throw SolidError(path.filename().string() + " holds no shape");
        return make(s);
    });
}

void Solid::writeStep(const std::filesystem::path& path, const std::string& name) const {
    if (empty()) throw SolidError("nothing to write");
    guarded("STEP", [&] {
        STEPControl_Writer w;
        Interface_Static::SetCVal("write.step.unit", "MM");
        Interface_Static::SetCVal("write.step.product.name", name.empty() ? "OpenRevers part" : name.c_str());
        if (w.Transfer(shapeOf(*this), STEPControl_AsIs) != IFSelect_RetDone) throw SolidError("the shape could not be put into STEP");
        if (w.Write(path.string().c_str()) != IFSelect_RetDone) throw SolidError("could not write " + path.string());
        return 0;
    });
}

std::string Solid::toBrep() const {
    if (empty()) return {};
    return guarded("BREP", [&] {
        std::ostringstream os;
        BRepTools::Write(shapeOf(*this), os);
        return os.str();
    });
}

Solid Solid::fromBrep(const std::string& text) {
    if (text.empty()) return {};
    return guarded("BREP", [&] {
        std::istringstream is(text);
        TopoDS_Shape s;
        BRep_Builder b;
        BRepTools::Read(s, is, b);
        if (s.IsNull()) throw SolidError("the solid in the project could not be read");
        return make(s);
    });
}

SolidTessellation Solid::tessellate(double deflection, double angleDeg) const {
    SolidTessellation t;
    if (empty()) return t;
    guarded("tessellate", [&] {
        const TopoDS_Shape& s = shapeOf(*this);
        BRepTools::Clean(s);
        BRepMesh_IncrementalMesh mesh(s, deflection, Standard_False, angleDeg * kPi / 180, Standard_True);
        std::uint32_t faceIndex = 0;
        for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next(), ++faceIndex) {
            const TopoDS_Face& f = TopoDS::Face(ex.Current());
            TopLoc_Location loc;
            const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(f, loc);
            if (tri.IsNull()) continue;
            const gp_Trsf tr = loc.Transformation();
            const auto base = static_cast<std::uint32_t>(t.mesh.vertices.size());
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                const gp_Pnt p = tri->Node(i).Transformed(tr);
                t.mesh.vertices.emplace_back(static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z()));
            }
            const bool reversed = f.Orientation() == TopAbs_REVERSED;
            for (int i = 1; i <= tri->NbTriangles(); ++i) {
                int a, b, c;
                tri->Triangle(i).Get(a, b, c);
                if (reversed) std::swap(b, c);
                t.mesh.faces.push_back({base + static_cast<std::uint32_t>(a - 1), base + static_cast<std::uint32_t>(b - 1), base + static_cast<std::uint32_t>(c - 1)});
                t.faceOf.push_back(faceIndex);
            }
        }
        return 0;
    });
    return t;
}

double Solid::volume() const {
    if (empty()) return 0;
    GProp_GProps g;
    BRepGProp::VolumeProperties(shapeOf(*this), g);
    return g.Mass();
}

double Solid::area() const {
    if (empty()) return 0;
    GProp_GProps g;
    BRepGProp::SurfaceProperties(shapeOf(*this), g);
    return g.Mass();
}

Eigen::AlignedBox3d Solid::bounds() const {
    Eigen::AlignedBox3d box;
    if (empty()) return box;
    Bnd_Box b;
    BRepBndLib::Add(shapeOf(*this), b);
    if (b.IsVoid()) return box;
    double x0, y0, z0, x1, y1, z1;
    b.Get(x0, y0, z0, x1, y1, z1);
    // Bnd_Box adds a small gap round the shape; take it off.
    const double gap = b.GetGap();
    box.extend(Vec3d(x0 + gap, y0 + gap, z0 + gap));
    box.extend(Vec3d(x1 - gap, y1 - gap, z1 - gap));
    return box;
}

namespace {
int count(const TopoDS_Shape& s, TopAbs_ShapeEnum what) {
    TopTools_IndexedMapOfShape m;
    TopExp::MapShapes(s, what, m);
    return m.Extent();
}
}  // namespace

int Solid::faceCount() const { return empty() ? 0 : count(shapeOf(*this), TopAbs_FACE); }
int Solid::edgeCount() const { return empty() ? 0 : count(shapeOf(*this), TopAbs_EDGE); }
int Solid::solidCount() const { return empty() ? 0 : count(shapeOf(*this), TopAbs_SOLID); }

std::string Solid::checkProblem() const {
    if (empty()) return "empty";
    BRepCheck_Analyzer a(shapeOf(*this));
    return a.IsValid() ? std::string() : std::string("the shape fails Open CASCADE's validity check");
}

std::string occtVersion() { return OCC_VERSION_COMPLETE; }

}  // namespace orv

// SPDX-License-Identifier: GPL-3.0-or-later
// The only file that talks to Open CASCADE. Written against the API common to
// 7.6 (Ubuntu 24.04) and 7.9 (the Windows build).
#include "core/solid/Solid.h"

#include "core/solid/BSpline.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
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
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <Precision.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
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
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cstdio>
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

/// The same clamped B-spline as BSpline.h, with 3-D poles in the plane; its
/// ends are exactly the neighbours' joints.
Handle(Geom_BSplineCurve) splineCurve(const Segment& s, const SketchPlane& pl, const Vec2d& a, const Vec2d& b) {
    const int n = static_cast<int>(s.poles.size());
    const int p = curveDegree(s.poles.size());
    TColgp_Array1OfPnt poles(1, n);
    for (int i = 0; i < n; ++i) {
        const Vec2d q = i == 0 ? a : i == n - 1 ? b : s.poles[static_cast<std::size_t>(i)];
        poles.SetValue(i + 1, pnt(pl.toWorld(q)));
    }
    std::vector<double> k;
    std::vector<int> m;
    knotsAndMultiplicities(clampedKnots(n, p), k, m);
    TColStd_Array1OfReal knots(1, static_cast<int>(k.size()));
    TColStd_Array1OfInteger mults(1, static_cast<int>(m.size()));
    for (std::size_t i = 0; i < k.size(); ++i) {
        knots.SetValue(static_cast<int>(i) + 1, k[i]);
        mults.SetValue(static_cast<int>(i) + 1, m[i]);
    }
    return new Geom_BSplineCurve(poles, knots, mults, p);
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
        } else if (s.kind == Segment::Kind::Spline) {
            mw.Add(BRepBuilderAPI_MakeEdge(splineCurve(s, pl, a, b)).Edge());
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

namespace {
/// The same surface as HeightSurface, as an Open CASCADE face.
TopoDS_Face surfaceFace(const HeightSurface& hs) {
    const int pu = std::min(3, hs.nu - 1), pv = std::min(3, hs.nv - 1);
    const auto P = hs.poles3();
    TColgp_Array2OfPnt poles(1, hs.nu, 1, hs.nv);
    for (int j = 0; j < hs.nv; ++j)
        for (int i = 0; i < hs.nu; ++i) poles.SetValue(i + 1, j + 1, pnt(P[static_cast<std::size_t>(i + hs.nu * j)]));
    struct Knots {
        TColStd_Array1OfReal k;
        TColStd_Array1OfInteger m;
    };
    auto knotsOf = [](int n, int p) {
        std::vector<double> kk;
        std::vector<int> mm;
        knotsAndMultiplicities(clampedKnots(n, p), kk, mm);
        Knots out{TColStd_Array1OfReal(1, static_cast<int>(kk.size())), TColStd_Array1OfInteger(1, static_cast<int>(mm.size()))};
        for (std::size_t i = 0; i < kk.size(); ++i) {
            out.k.SetValue(static_cast<int>(i) + 1, kk[i]);
            out.m.SetValue(static_cast<int>(i) + 1, mm[i]);
        }
        return out;
    };
    const Knots u = knotsOf(hs.nu, pu), v = knotsOf(hs.nv, pv);
    Handle(Geom_BSplineSurface) g = new Geom_BSplineSurface(poles, u.k, v.k, u.m, v.m, pu, pv);
    BRepBuilderAPI_MakeFace mf(g, Precision::Confusion());
    if (!mf.IsDone()) throw SolidError("the surface could not be made into a face");
    return mf.Face();
}
}  // namespace

Solid Solid::loft(const std::vector<Profile>& sections, bool ruled) {
    if (sections.size() < 2) throw SolidError("a loft needs two sections at least");
    const std::size_t k = sections.front().loops.size();
    for (const auto& p : sections) {
        if (const auto why = p.problem(); !why.empty()) throw SolidError(why);
        if (p.loops.empty()) throw SolidError("a section of a loft has no loop");
        if (p.loops.size() != k)
            throw SolidError("every section of a loft must have the same number of loops (one outline, or an outline and the same holes)");
    }
    // Each section's loops in order: the largest (the outline) first, then
    // each hole matched to the nearest one of the section before.
    auto centre = [](const Loop& l, const SketchPlane& pl) {
        const auto poly = l.polygon(0.5);
        Vec2d c = Vec2d::Zero();
        for (const auto& q : poly) c += q;
        if (!poly.empty()) c /= static_cast<double>(poly.size());
        return pl.toWorld(c);
    };
    std::vector<std::vector<const Loop*>> order(sections.size());
    for (std::size_t s = 0; s < sections.size(); ++s) {
        SketchPlane pl = sections[s].plane;
        pl.normalize();
        std::vector<const Loop*> ls;
        for (const auto& l : sections[s].loops) ls.push_back(&l);
        std::sort(ls.begin(), ls.end(), [](const Loop* a, const Loop* b) { return std::abs(a->area()) > std::abs(b->area()); });
        if (s == 0) { order[s] = ls; continue; }
        SketchPlane prev = sections[s - 1].plane;
        prev.normalize();
        std::vector<const Loop*> o{ls.front()};
        std::vector<bool> used(ls.size(), false);
        used[0] = true;
        for (std::size_t i = 1; i < k; ++i) {
            const Vec3d want = centre(*order[s - 1][i], prev);
            std::size_t best = 0;
            double bestD = 1e300;
            for (std::size_t j = 1; j < ls.size(); ++j) {
                if (used[j]) continue;
                const double d = (centre(*ls[j], pl) - want).norm();
                if (d < bestD) { bestD = d; best = j; }
            }
            used[best] = true;
            o.push_back(ls[best]);
        }
        order[s] = o;
    }
    auto loftOf = [&](std::size_t i) {
        return guarded("loft", [&] {
            BRepOffsetAPI_ThruSections ts(Standard_True, ruled ? Standard_True : Standard_False);
            for (std::size_t s = 0; s < sections.size(); ++s) {
                SketchPlane pl = sections[s].plane;
                pl.normalize();
                ts.AddWire(wireOf(*order[s][i], pl));
            }
            ts.Build();
            if (!ts.IsDone()) throw SolidError("the loft failed (do the sections face the same way, with their starts lined up?)");
            return make(ts.Shape());
        });
    };
    // The outline's loft, less the holes' lofts: a hollow loft (a wall).
    Solid out = loftOf(0);
    for (std::size_t i = 1; i < k; ++i) out = out.cut(loftOf(i));
    return out;
}

Solid Solid::thicken(const HeightSurface& hs, double thickness) {
    if (std::abs(thickness) < 1e-6) throw SolidError("a thickness cannot be zero");
    return guarded("thicken", [&] {
        BRepOffsetAPI_MakeThickSolid mk;
        mk.MakeThickSolidBySimple(surfaceFace(hs), thickness);
        if (!mk.IsDone()) throw SolidError("the surface could not be thickened (too thick for its bends?)");
        // Its faces may come out facing in: turn every solid outward.
        TopoDS_Shape out = mk.Shape();
        if (out.ShapeType() == TopAbs_SOLID) {
            TopoDS_Solid so = TopoDS::Solid(out);
            BRepLib::OrientClosedSolid(so);
            out = so;
        } else {
            BRep_Builder bb;
            TopoDS_Compound c;
            bb.MakeCompound(c);
            for (TopExp_Explorer ex(out, TopAbs_SOLID); ex.More(); ex.Next()) {
                TopoDS_Solid so = TopoDS::Solid(ex.Current());
                BRepLib::OrientClosedSolid(so);
                bb.Add(c, so);
            }
            out = c;
        }
        return make(out);
    });
}

Solid Solid::cutBySurface(const HeightSurface& hs, bool keepBelow) const {
    if (empty()) throw SolidError("there is nothing to cut yet");
    // The surface must cover the whole part seen from its plane: a smaller
    // one cuts only inside its own outline and leaves stray pieces.
    {
        const auto b = bounds();
        const auto t = tessellate(std::max(0.05, (b.max() - b.min()).norm() / 300.0), 30.0);
        double short_ = 0;
        for (const auto& p : t.mesh.vertices) {
            const Vec3d d = p.cast<double>() - hs.origin;
            const double x = d.dot(hs.xDir), y = d.dot(hs.yDir);
            short_ = std::max({short_, hs.x0 - x, x - (hs.x0 + hs.width), hs.y0 - y, y - (hs.y0 + hs.height)});
        }
        if (short_ > 1e-3) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.2f", short_);
            throw SolidError(std::string("the surface does not cover the part: it falls ") + buf +
                             " mm short of its edge seen from the surface's plane (fit it with a larger margin, or over a larger zone)");
        }
    }
    return guarded("cut by surface", [&] {
        // The surface swept far to the side to remove: a solid to cut away.
        const auto b = bounds();
        const double reach = 4 * (b.max() - b.min()).norm() + 4 * std::abs(hs.width) + 4 * std::abs(hs.height) + 10;
        const gp_Vec v = gp_Vec(dir(hs.normal)) * (keepBelow ? reach : -reach);
        BRepPrimAPI_MakePrism prism(surfaceFace(hs), v);
        if (!prism.IsDone()) throw SolidError("the surface could not be swept");
        BRepAlgoAPI_Cut op(shapeOf(*this), prism.Shape());
        if (!op.IsDone()) throw SolidError("cutting by the surface failed (does it reach past the part?)");
        return make(unify(op.Shape()));
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
        // The edges, from their curves (the same order as edgeCount).
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(s, TopAbs_EDGE, edges);
        for (int i = 1; i <= edges.Extent(); ++i) {
            const TopoDS_Edge& e = TopoDS::Edge(edges(i));
            std::vector<Vec3f> line;
            Vec3d middle = Vec3d::Zero();
            if (!BRep_Tool::Degenerated(e)) {
                BRepAdaptor_Curve c(e);
                GCPnts_QuasiUniformDeflection d(c, deflection);
                if (d.IsDone())
                    for (int k = 1; k <= d.NbPoints(); ++k) {
                        const gp_Pnt p = d.Value(k);
                        line.emplace_back(static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z()));
                    }
                const gp_Pnt m = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
                middle = {m.X(), m.Y(), m.Z()};
            }
            t.edges.push_back(std::move(line));
            t.edgeMiddle.push_back(middle);
        }
        return 0;
    });
    return t;
}

ShapeInfo Solid::faceInfo(int index) const {
    ShapeInfo info;
    if (empty()) return info;
    return guarded("face", [&] {
        int k = 0;
        for (TopExp_Explorer ex(shapeOf(*this), TopAbs_FACE); ex.More(); ex.Next(), ++k) {
            if (k != index) continue;
            const TopoDS_Face& f = TopoDS::Face(ex.Current());
            BRepAdaptor_Surface a(f);
            auto v3 = [](const gp_XYZ& p) { return Vec3d(p.X(), p.Y(), p.Z()); };
            switch (a.GetType()) {
                case GeomAbs_Plane: {
                    info.type = "plane";
                    const gp_Pln pl = a.Plane();
                    info.origin = v3(pl.Location().XYZ());
                    gp_Dir n = pl.Axis().Direction();
                    if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
                    info.axis = v3(n.XYZ());
                    // A point inside the face is a better origin than the plane's own.
                    const double u = 0.5 * (a.FirstUParameter() + a.LastUParameter()), w = 0.5 * (a.FirstVParameter() + a.LastVParameter());
                    info.origin = v3(a.Value(u, w).XYZ());
                    break;
                }
                case GeomAbs_Cylinder:
                    info.type = "cylinder";
                    info.origin = v3(a.Cylinder().Location().XYZ());
                    info.axis = v3(a.Cylinder().Axis().Direction().XYZ());
                    info.radius = a.Cylinder().Radius();
                    break;
                case GeomAbs_Cone:
                    info.type = "cone";
                    info.origin = v3(a.Cone().Location().XYZ());
                    info.axis = v3(a.Cone().Axis().Direction().XYZ());
                    info.radius = a.Cone().RefRadius();
                    info.angleDeg = a.Cone().SemiAngle() * 180 / kPi;
                    break;
                case GeomAbs_Sphere:
                    info.type = "sphere";
                    info.origin = v3(a.Sphere().Location().XYZ());
                    info.radius = a.Sphere().Radius();
                    break;
                case GeomAbs_Torus:
                    info.type = "torus";
                    info.origin = v3(a.Torus().Location().XYZ());
                    info.axis = v3(a.Torus().Axis().Direction().XYZ());
                    info.radius = a.Torus().MajorRadius();
                    info.radius2 = a.Torus().MinorRadius();
                    break;
                case GeomAbs_BSplineSurface:
                case GeomAbs_BezierSurface: info.type = "bspline"; break;
                default: info.type = "other"; break;
            }
            GProp_GProps g;
            BRepGProp::SurfaceProperties(f, g);
            info.size = g.Mass();
            return info;
        }
        return info;
    });
}

ShapeInfo Solid::edgeInfo(int index) const {
    ShapeInfo info;
    if (empty()) return info;
    return guarded("edge", [&] {
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(shapeOf(*this), TopAbs_EDGE, edges);
        if (index < 0 || index >= edges.Extent()) return info;
        const TopoDS_Edge& e = TopoDS::Edge(edges(index + 1));
        if (BRep_Tool::Degenerated(e)) return info;
        BRepAdaptor_Curve c(e);
        auto v3 = [](const gp_XYZ& p) { return Vec3d(p.X(), p.Y(), p.Z()); };
        switch (c.GetType()) {
            case GeomAbs_Line:
                info.type = "line";
                info.origin = v3(c.Value(c.FirstParameter()).XYZ());
                info.axis = v3(c.Line().Direction().XYZ());
                break;
            case GeomAbs_Circle:
                info.type = "circle";
                info.origin = v3(c.Circle().Location().XYZ());
                info.axis = v3(c.Circle().Axis().Direction().XYZ());
                info.radius = c.Circle().Radius();
                break;
            case GeomAbs_Ellipse: info.type = "ellipse"; break;
            case GeomAbs_BSplineCurve:
            case GeomAbs_BezierCurve: info.type = "bspline"; break;
            default: info.type = "other"; break;
        }
        info.size = GCPnts_AbscissaPoint::Length(c);
        return info;
    });
}

namespace {
/// Whether a shape has free-form edges or faces. Open CASCADE's integration
/// (BRepGProp, a fixed number of Gauss points) is off by up to 1% on faces
/// bounded by B-splines with many knots; their volume and area come from a
/// fine triangulation instead (exact to the deflection).
bool freeForm(const TopoDS_Shape& s) {
    for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
        const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
        if (BRep_Tool::Degenerated(e)) continue;
        const auto t = BRepAdaptor_Curve(e).GetType();
        if (t == GeomAbs_BSplineCurve || t == GeomAbs_BezierCurve || t == GeomAbs_OtherCurve || t == GeomAbs_OffsetCurve) return true;
    }
    for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
        const auto t = BRepAdaptor_Surface(TopoDS::Face(ex.Current())).GetType();
        if (t == GeomAbs_BSplineSurface || t == GeomAbs_BezierSurface || t == GeomAbs_OtherSurface || t == GeomAbs_OffsetSurface) return true;
    }
    return false;
}
}  // namespace

double Solid::volume() const {
    if (empty()) return 0;
    if (freeForm(shapeOf(*this))) {
        const auto b = bounds();
        return tessellate(std::max(1e-5, 1e-4 * (b.max() - b.min()).norm()), 5.0).mesh.signedVolume();
    }
    GProp_GProps g;
    BRepGProp::VolumeProperties(shapeOf(*this), g);
    return g.Mass();
}

double Solid::area() const {
    if (empty()) return 0;
    if (freeForm(shapeOf(*this))) {
        const auto b = bounds();
        return tessellate(std::max(1e-5, 1e-4 * (b.max() - b.min()).norm()), 5.0).mesh.area();
    }
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

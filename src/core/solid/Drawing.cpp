// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/solid/Drawing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace orv {
namespace {

using Vec2 = Eigen::Vector2d;
using Vec3 = Eigen::Vector3d;
constexpr double kPi = 3.14159265358979323846;
constexpr char kDia = '\x01';  // the diameter sign in a text, written as each format needs

struct ViewDef {
    const char* name;
    Vec3 eye, up;
};
const std::array<ViewDef, 6>& viewDefs() {
    static const std::array<ViewDef, 6> defs = {{{"front", {0, -1, 0}, {0, 0, 1}},
                                                 {"back", {0, 1, 0}, {0, 0, 1}},
                                                 {"top", {0, 0, 1}, {0, 1, 0}},
                                                 {"bottom", {0, 0, -1}, {0, -1, 0}},
                                                 {"right", {1, 0, 0}, {0, 0, 1}},
                                                 {"left", {-1, 0, 0}, {0, 0, 1}}}};
    return defs;
}

/// The triangles of the solid seen from one direction, on a grid, to ask whether
/// a point is hidden behind them.
class Occluder {
public:
    Occluder(const TriMesh& m, const Vec3& eye, const Vec3& right, const Vec3& up, double minX, double minY, double maxX, double maxY)
        : eye_(eye), right_(right), up_(up), minX_(minX), minY_(minY) {
        w_ = std::max(1e-6, maxX - minX);
        h_ = std::max(1e-6, maxY - minY);
        n_ = std::clamp(static_cast<int>(std::sqrt(static_cast<double>(m.faces.size())) * 1.5), 8, 512);
        cells_.assign(static_cast<std::size_t>(n_) * static_cast<std::size_t>(n_), {});
        tris_.reserve(m.faces.size());
        for (const auto& f : m.faces) {
            Tri t;
            for (int k = 0; k < 3; ++k) {
                const Vec3 p = m.vertices[f[static_cast<std::size_t>(k)]].cast<double>();
                t.x[static_cast<std::size_t>(k)] = p.dot(right_);
                t.y[static_cast<std::size_t>(k)] = p.dot(up_);
                t.z[static_cast<std::size_t>(k)] = p.dot(eye_);
            }
            const double det = (t.x[1] - t.x[0]) * (t.y[2] - t.y[0]) - (t.x[2] - t.x[0]) * (t.y[1] - t.y[0]);
            if (std::abs(det) < 1e-12) continue;  // edge-on: hides nothing
            t.det = det;
            const auto id = static_cast<std::uint32_t>(tris_.size());
            tris_.push_back(t);
            const double x0 = std::min({t.x[0], t.x[1], t.x[2]}), x1 = std::max({t.x[0], t.x[1], t.x[2]});
            const double y0 = std::min({t.y[0], t.y[1], t.y[2]}), y1 = std::max({t.y[0], t.y[1], t.y[2]});
            for (int j = cell(y0, minY_, h_); j <= cell(y1, minY_, h_); ++j)
                for (int i = cell(x0, minX_, w_); i <= cell(x1, minX_, w_); ++i) cells_[static_cast<std::size_t>(j) * static_cast<std::size_t>(n_) + static_cast<std::size_t>(i)].push_back(id);
        }
    }

    /// Is a point at (x, y) with depth z (towards the eye) behind a triangle by more than `gap`?
    bool hidden(double x, double y, double z, double gap) const {
        const auto& list = cells_[static_cast<std::size_t>(cell(y, minY_, h_)) * static_cast<std::size_t>(n_) + static_cast<std::size_t>(cell(x, minX_, w_))];
        for (auto id : list) {
            const Tri& t = tris_[id];
            const double dx = x - t.x[0], dy = y - t.y[0];
            const double u = (dx * (t.y[2] - t.y[0]) - dy * (t.x[2] - t.x[0])) / t.det;
            const double v = (dy * (t.x[1] - t.x[0]) - dx * (t.y[1] - t.y[0])) / t.det;
            constexpr double e = 1e-7;
            if (u < -e || v < -e || u + v > 1 + e) continue;
            const double depth = t.z[0] + u * (t.z[1] - t.z[0]) + v * (t.z[2] - t.z[0]);
            if (depth > z + gap) return true;
        }
        return false;
    }

private:
    struct Tri {
        std::array<double, 3> x{}, y{}, z{};
        double det{0};
    };
    int cell(double v, double lo, double span) const { return std::clamp(static_cast<int>((v - lo) / span * n_), 0, n_ - 1); }
    Vec3 eye_, right_, up_;
    double minX_, minY_, w_{1}, h_{1};
    int n_{8};
    std::vector<Tri> tris_;
    std::vector<std::vector<std::uint32_t>> cells_;
};

struct Quant {
    static std::int64_t key(double v) { return static_cast<std::int64_t>(std::llround(v * 1e4)); }
};

struct Raw {  // a segment in 3D before projection
    Vec3 a, b;
};

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

// ------------------------------------------------------------- the sheet --

enum class Style { Visible, Hidden, Centre, Thin };
struct Prim {
    enum Kind { Line, Text, Arrow } kind{Line};
    Style style{Style::Visible};
    Vec2 a{Vec2::Zero()}, b{Vec2::Zero()}, c{Vec2::Zero()};  // line: a-b; arrow: tip a, pointing along b (unit), size c.x; text: at a
    std::string text;
    double height{3.5}, angle{0};
    bool centred{false};
};

void arrow(std::vector<Prim>& out, const Vec2& tip, const Vec2& dir) {
    Prim p;
    p.kind = Prim::Arrow;
    p.style = Style::Thin;
    p.a = tip;
    p.b = dir;
    p.c = {3.0, 0.9};
    out.push_back(p);
}
void line(std::vector<Prim>& out, const Vec2& a, const Vec2& b, Style s) {
    Prim p;
    p.style = s;
    p.a = a;
    p.b = b;
    out.push_back(p);
}
void text(std::vector<Prim>& out, const Vec2& at, const std::string& t, double angle = 0, bool centred = true) {
    Prim p;
    p.kind = Prim::Text;
    p.style = Style::Thin;
    p.a = at;
    p.text = t;
    p.angle = angle;
    p.centred = centred;
    out.push_back(p);
}

std::vector<Prim> layoutSheet(const Drawing& d) {
    std::vector<Prim> out;
    const double m = 10;  // margin
    line(out, {m, m}, {d.sheetW - m, m}, Style::Thin);
    line(out, {d.sheetW - m, m}, {d.sheetW - m, d.sheetH - m}, Style::Thin);
    line(out, {d.sheetW - m, d.sheetH - m}, {m, d.sheetH - m}, Style::Thin);
    line(out, {m, d.sheetH - m}, {m, m}, Style::Thin);
    line(out, {m, m + 10}, {d.sheetW - m, m + 10}, Style::Thin);
    text(out, {m + 3, m + 3.2}, d.title.empty() ? "OpenRevers" : d.title, 0, false);
    text(out, {d.sheetW - m - 3, m + 3.2}, "Scale " + d.scaleText + "  -  mm", 0, false);
    out.back().centred = false;
    out.back().angle = 0;
    out.back().height = -3.5;  // right-aligned marker (negative height), see the writers

    for (const auto& v : d.views) {
        const Vec2 o = v.origin;
        const double s = d.scale;
        for (const auto& g : v.segments) line(out, o + g.a * s, o + g.b * s, g.centre ? Style::Centre : g.hidden ? Style::Hidden : Style::Visible);
        for (const auto& dim : v.dimensions) {
            if (!dim.diameter) {
                const Vec2 a = o + dim.a * s, b = o + dim.b * s;
                const Vec2 n = dim.offsetDir.normalized();
                const Vec2 pa = a + n * dim.offset, pb = b + n * dim.offset;
                line(out, a + n * 1.0, pa + n * 2.0, Style::Thin);
                line(out, b + n * 1.0, pb + n * 2.0, Style::Thin);
                line(out, pa, pb, Style::Thin);
                const Vec2 t = (pb - pa).norm() > 1e-9 ? (pb - pa).normalized() : Vec2(1, 0);
                arrow(out, pa, t);
                arrow(out, pb, -t);
                const bool vertical = std::abs(t.y()) > std::abs(t.x());
                const Vec2 mid = 0.5 * (pa + pb);
                if (vertical) text(out, mid + Vec2(-1.2, 0), number(dim.value), 90);
                else text(out, mid + Vec2(0, 1.2), number(dim.value));
            } else {
                const Vec2 c = o + dim.centre * s;
                const double r = 0.5 * dim.value * s;
                const Vec2 dir = Vec2(std::cos(kPi / 4), std::sin(kPi / 4));
                const Vec2 edge = c + dir * r;
                const Vec2 knee = edge + dir * 8.0;
                line(out, edge, knee, Style::Thin);
                line(out, knee, knee + Vec2(6, 0), Style::Thin);
                arrow(out, edge, -dir);
                text(out, knee + Vec2(1.0, 1.0), std::string(1, kDia) + number(dim.value), 0, false);
            }
        }
    }
    return out;
}

// --------------------------------------------------------------- writers --

std::string dxfText(const std::string& t) {
    std::string s;
    for (char ch : t) {
        if (ch == kDia) s += "%%c";
        else s += ch;
    }
    return s;
}

void writeDxf(const std::vector<Prim>& prims, const Drawing& d, std::ostream& os) {
    auto pair = [&](int code, const std::string& v) { os << code << "\n" << v << "\n"; };
    auto num = [&](int code, double v) { char b[40]; std::snprintf(b, sizeof b, "%.5f", v); pair(code, b); };
    pair(0, "SECTION"); pair(2, "HEADER"); pair(9, "$ACADVER"); pair(1, "AC1009"); pair(9, "$INSUNITS"); pair(70, "4"); pair(0, "ENDSEC");
    pair(0, "SECTION"); pair(2, "TABLES");
    pair(0, "TABLE"); pair(2, "LTYPE"); pair(70, "3");
    pair(0, "LTYPE"); pair(2, "CONTINUOUS"); pair(70, "0"); pair(3, "Solid line"); pair(72, "65"); pair(73, "0"); num(40, 0);
    pair(0, "LTYPE"); pair(2, "DASHED"); pair(70, "0"); pair(3, "Dashed"); pair(72, "65"); pair(73, "2"); num(40, 6); num(49, 4); num(49, -2);
    pair(0, "LTYPE"); pair(2, "CENTER"); pair(70, "0"); pair(3, "Centre"); pair(72, "65"); pair(73, "4"); num(40, 14); num(49, 8); num(49, -2); num(49, 2); num(49, -2);
    pair(0, "ENDTAB");
    pair(0, "TABLE"); pair(2, "LAYER"); pair(70, "4");
    for (const auto& [name, color, lt] : std::vector<std::tuple<std::string, int, std::string>>{{"VISIBLE", 7, "CONTINUOUS"}, {"HIDDEN", 8, "DASHED"}, {"CENTRE", 1, "CENTER"}, {"DIMENSIONS", 3, "CONTINUOUS"}}) {
        pair(0, "LAYER"); pair(2, name); pair(70, "0"); pair(62, std::to_string(color)); pair(6, lt);
    }
    pair(0, "ENDTAB"); pair(0, "ENDSEC");
    pair(0, "SECTION"); pair(2, "ENTITIES");
    auto layer = [&](Style s) { return s == Style::Visible ? "VISIBLE" : s == Style::Hidden ? "HIDDEN" : s == Style::Centre ? "CENTRE" : "DIMENSIONS"; };
    for (const auto& p : prims) {
        if (p.kind == Prim::Line) {
            pair(0, "LINE"); pair(8, layer(p.style));
            num(10, p.a.x()); num(20, p.a.y()); num(30, 0); num(11, p.b.x()); num(21, p.b.y()); num(31, 0);
        } else if (p.kind == Prim::Arrow) {
            const Vec2 back = p.a + p.b * p.c.x();
            const Vec2 nrm(-p.b.y(), p.b.x());
            const Vec2 l = back + nrm * p.c.y(), r = back - nrm * p.c.y();
            pair(0, "SOLID"); pair(8, "DIMENSIONS");
            num(10, p.a.x()); num(20, p.a.y()); num(30, 0); num(11, l.x()); num(21, l.y()); num(31, 0);
            num(12, r.x()); num(22, r.y()); num(32, 0); num(13, r.x()); num(23, r.y()); num(33, 0);
        } else {
            const bool right = p.height < 0;
            pair(0, "TEXT"); pair(8, "DIMENSIONS");
            num(10, p.a.x()); num(20, p.a.y()); num(30, 0);
            num(40, std::abs(p.height)); pair(1, dxfText(p.text)); num(50, p.angle);
            if (p.centred || right) {
                pair(72, right ? "2" : "1"); pair(73, "0");
                num(11, p.a.x()); num(21, p.a.y()); num(31, 0);
            }
        }
    }
    pair(0, "ENDSEC"); pair(0, "EOF");
    (void)d;
}

std::string xmlEscape(const std::string& t) {
    std::string s;
    for (char ch : t) {
        if (ch == kDia) s += "\xC3\x98";
        else if (ch == '&') s += "&amp;";
        else if (ch == '<') s += "&lt;";
        else if (ch == '>') s += "&gt;";
        else s += ch;
    }
    return s;
}

void writeSvg(const std::vector<Prim>& prims, const Drawing& d, std::ostream& os) {
    auto n = [](double v) { char b[32]; std::snprintf(b, sizeof b, "%.3f", v); return std::string(b); };
    const double H = d.sheetH;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << n(d.sheetW) << "mm\" height=\"" << n(H) << "mm\" viewBox=\"0 0 " << n(d.sheetW) << " "
       << n(H) << "\">\n<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/>\n<g fill=\"none\" stroke=\"#000\" stroke-linecap=\"round\" font-family=\"Helvetica, Arial, sans-serif\">\n";
    for (const auto& p : prims) {
        if (p.kind == Prim::Line) {
            const char* attr = p.style == Style::Visible ? "stroke-width=\"0.35\""
                               : p.style == Style::Hidden ? "stroke-width=\"0.18\" stroke-dasharray=\"2.5 1.2\""
                               : p.style == Style::Centre ? "stroke-width=\"0.18\" stroke-dasharray=\"6 1.2 1 1.2\""
                                                          : "stroke-width=\"0.18\"";
            os << "<line x1=\"" << n(p.a.x()) << "\" y1=\"" << n(H - p.a.y()) << "\" x2=\"" << n(p.b.x()) << "\" y2=\"" << n(H - p.b.y()) << "\" " << attr << "/>\n";
        } else if (p.kind == Prim::Arrow) {
            const Vec2 back = p.a + p.b * p.c.x();
            const Vec2 nrm(-p.b.y(), p.b.x());
            const Vec2 l = back + nrm * p.c.y(), r = back - nrm * p.c.y();
            os << "<polygon points=\"" << n(p.a.x()) << "," << n(H - p.a.y()) << " " << n(l.x()) << "," << n(H - l.y()) << " " << n(r.x()) << "," << n(H - r.y())
               << "\" fill=\"#000\" stroke=\"none\"/>\n";
        } else {
            const bool right = p.height < 0;
            os << "<text x=\"" << n(p.a.x()) << "\" y=\"" << n(H - p.a.y()) << "\" font-size=\"" << n(std::abs(p.height)) << "\" fill=\"#000\" stroke=\"none\" text-anchor=\""
               << (right ? "end" : p.centred ? "middle" : "start") << "\"";
            if (p.angle != 0) os << " transform=\"rotate(" << n(-p.angle) << " " << n(p.a.x()) << " " << n(H - p.a.y()) << ")\"";
            os << ">" << xmlEscape(p.text) << "</text>\n";
        }
    }
    os << "</g>\n</svg>\n";
}

std::string pdfText(const std::string& t) {
    std::string s;
    for (char ch : t) {
        if (ch == kDia) s += "\\330";
        else if (ch == '(' || ch == ')' || ch == '\\') { s += '\\'; s += ch; }
        else s += ch;
    }
    return s;
}

void writePdf(const std::vector<Prim>& prims, const Drawing& d, std::ostream& os) {
    constexpr double pt = 72.0 / 25.4;
    auto n = [](double v) { char b[32]; std::snprintf(b, sizeof b, "%.3f", v); return std::string(b); };
    std::ostringstream c;
    c << "1 J 1 j\n";
    for (const auto& p : prims) {
        if (p.kind == Prim::Line) {
            const bool vis = p.style == Style::Visible;
            c << n(vis ? 0.35 * pt : 0.18 * pt) << " w ";
            if (p.style == Style::Hidden) c << "[" << n(2.5 * pt) << " " << n(1.2 * pt) << "] 0 d ";
            else if (p.style == Style::Centre) c << "[" << n(6 * pt) << " " << n(1.2 * pt) << " " << n(1 * pt) << " " << n(1.2 * pt) << "] 0 d ";
            else c << "[] 0 d ";
            c << n(p.a.x() * pt) << " " << n(p.a.y() * pt) << " m " << n(p.b.x() * pt) << " " << n(p.b.y() * pt) << " l S\n";
        } else if (p.kind == Prim::Arrow) {
            const Vec2 back = p.a + p.b * p.c.x();
            const Vec2 nrm(-p.b.y(), p.b.x());
            const Vec2 l = back + nrm * p.c.y(), r = back - nrm * p.c.y();
            c << n(p.a.x() * pt) << " " << n(p.a.y() * pt) << " m " << n(l.x() * pt) << " " << n(l.y() * pt) << " l " << n(r.x() * pt) << " " << n(r.y() * pt) << " l f\n";
        } else {
            const double h = std::abs(p.height);
            const bool right = p.height < 0;
            // Helvetica's average advance is about 0.55 em: centring and right alignment by that estimate.
            const double width = static_cast<double>(p.text.size()) * 0.55 * h;
            const double shift = right ? -width : p.centred ? -0.5 * width : 0.0;
            const double ang = p.angle * kPi / 180.0;
            const double ca = std::cos(ang), sa = std::sin(ang);
            const double x = p.a.x() + shift * ca, y = p.a.y() + shift * sa;
            c << "BT /F1 " << n(h * pt) << " Tf " << n(ca) << " " << n(sa) << " " << n(-sa) << " " << n(ca) << " " << n(x * pt) << " " << n(y * pt) << " Tm (" << pdfText(p.text) << ") Tj ET\n";
        }
    }
    const std::string content = c.str();
    std::vector<std::size_t> offsets;
    std::ostringstream body;
    auto obj = [&](const std::string& s) {
        offsets.push_back(static_cast<std::size_t>(body.tellp()) + 9);  // after "%PDF-1.4\n" and the binary comment (see below)
        body << offsets.size() << " 0 obj\n" << s << "\nendobj\n";
    };
    obj("<< /Type /Catalog /Pages 2 0 R >>");
    obj("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + n(d.sheetW * pt) + " " + n(d.sheetH * pt) + "] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>");
    obj("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream");
    obj("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    const std::string head = "%PDF-1.4\n";
    os << head;
    const std::string b = body.str();
    os << b;
    const std::size_t xref = head.size() + b.size();
    os << "xref\n0 " << offsets.size() + 1 << "\n0000000000 65535 f \n";
    for (auto off : offsets) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off - 9 + head.size());
        os << buf;
    }
    os << "trailer\n<< /Size " << offsets.size() + 1 << " /Root 1 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";
}

}  // namespace

// --------------------------------------------------------------- the views --

Drawing makeDrawing(const Solid& solid, const DrawingOptions& opt) {
    if (solid.empty()) throw SolidError("there is no solid to draw");
    const SolidTessellation tess = solid.tessellate(std::max(0.002, opt.deflection));
    struct Chosen { const ViewDef* def; };
    std::vector<const ViewDef*> chosen;
    for (const auto& name : opt.views) {
        const ViewDef* found = nullptr;
        for (const auto& v : viewDefs())
            if (name == v.name) found = &v;
        if (!found) throw SolidError("unknown view '" + name + "' (front, back, top, bottom, right, left)");
        chosen.push_back(found);
    }
    if (chosen.empty()) throw SolidError("no views asked for");

    // Circular edges that are whole circles, to dimension the holes seen end-on.
    struct Circ { Vec3 centre, axis; double radius; };
    std::vector<Circ> circles;
    for (std::size_t i = 0; i < tess.edges.size(); ++i) {
        const ShapeInfo e = solid.edgeInfo(static_cast<int>(i));
        if (e.type != "circle" || e.radius <= 1e-6) continue;
        if (std::abs(e.size - 2 * kPi * e.radius) > 0.02 * e.size) continue;  // an arc
        circles.push_back({e.origin, e.axis.normalized(), e.radius});
    }

    Drawing d;
    d.title = opt.title;
    const Solid* sp = &solid;
    (void)sp;
    for (const ViewDef* def : chosen) {
        const Vec3 eye = def->eye, up = def->up, right = up.cross(eye);
        std::vector<Raw> raw;
        // The solid's own edges.
        for (const auto& poly : tess.edges)
            for (std::size_t k = 1; k < poly.size(); ++k) raw.push_back({poly[k - 1].cast<double>(), poly[k].cast<double>()});
        // Silhouettes: a mesh edge with a face towards the eye on one side and away on the other.
        {
            struct EdgeUse { Vec3 a, b; double facing; };
            std::unordered_map<std::uint64_t, std::vector<EdgeUse>> uses;
            auto vkey = [&](const Vec3& p) {
                const auto x = static_cast<std::uint64_t>(Quant::key(p.x()) & 0x1FFFFF), y = static_cast<std::uint64_t>(Quant::key(p.y()) & 0x1FFFFF), z = static_cast<std::uint64_t>(Quant::key(p.z()) & 0x1FFFFF);
                return (x << 42) | (y << 21) | z;
            };
            for (std::size_t f = 0; f < tess.mesh.faces.size(); ++f) {
                const Vec3 nrm = tess.mesh.faceNormal(f).cast<double>();
                const double facing = nrm.dot(eye);
                for (int k = 0; k < 3; ++k) {
                    const Vec3 a = tess.mesh.vertices[tess.mesh.faces[f][static_cast<std::size_t>(k)]].cast<double>();
                    const Vec3 b = tess.mesh.vertices[tess.mesh.faces[f][static_cast<std::size_t>((k + 1) % 3)]].cast<double>();
                    std::uint64_t ka = vkey(a), kb = vkey(b);
                    if (ka > kb) std::swap(ka, kb);
                    uses[ka * 1000003ULL ^ (kb * 0x9E3779B97F4A7C15ULL)].push_back({a, b, facing});
                }
            }
            for (const auto& [key, list] : uses) {
                (void)key;
                if (list.size() != 2) continue;
                constexpr double eps = 1e-6;
                if ((list[0].facing > eps && list[1].facing < -eps) || (list[0].facing < -eps && list[1].facing > eps)) raw.push_back({list[0].a, list[0].b});
            }
        }
        // The extent of the view.
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        for (const auto& s : raw)
            for (const Vec3* p : {&s.a, &s.b}) {
                minX = std::min(minX, p->dot(right)); maxX = std::max(maxX, p->dot(right));
                minY = std::min(minY, p->dot(up));    maxY = std::max(maxY, p->dot(up));
            }
        for (const auto& v : tess.mesh.vertices) {
            const Vec3 p = v.cast<double>();
            minX = std::min(minX, p.dot(right)); maxX = std::max(maxX, p.dot(right));
            minY = std::min(minY, p.dot(up));    maxY = std::max(maxY, p.dot(up));
        }
        const Occluder occ(tess.mesh, eye, right, up, minX, minY, maxX, maxY);
        const double gap = std::max(0.01, 2.5 * opt.deflection);

        DrawView view;
        view.name = def->name;
        view.size = {maxX - minX, maxY - minY};
        auto to2 = [&](const Vec3& p) { return Vec2(p.dot(right) - minX, p.dot(up) - minY); };
        std::size_t budget = 400000;
        for (const auto& s : raw) {
            const double len = (s.b - s.a).norm();
            if (len < 1e-6) continue;
            const int parts = budget < 8 ? 1 : std::clamp(static_cast<int>(std::ceil(len / std::max(0.5, len / 24.0))), 1, 64);
            budget -= std::min<std::size_t>(budget, static_cast<std::size_t>(parts));
            bool prev = false;
            Vec3 runStart = s.a;
            for (int k = 0; k < parts; ++k) {
                const Vec3 p0 = s.a + (s.b - s.a) * (static_cast<double>(k) / parts), p1 = s.a + (s.b - s.a) * (static_cast<double>(k + 1) / parts);
                const Vec3 mid = 0.5 * (p0 + p1);
                const bool hid = occ.hidden(mid.dot(right), mid.dot(up), mid.dot(eye), gap);
                if (k == 0) { prev = hid; runStart = p0; }
                else if (hid != prev) {
                    if (!prev || opt.hidden) view.segments.push_back({to2(runStart), to2(p0), prev, false});
                    runStart = p0;
                    prev = hid;
                }
            }
            if (!prev || opt.hidden) view.segments.push_back({to2(runStart), to2(s.b), prev, false});
        }
        if (opt.dimensions) {
            const double w = view.size.x(), h = view.size.y();
            if (w > 0.01) view.dimensions.push_back({false, {0, 0}, {w, 0}, {0, -1}, 9.0, {0, 0}, w});
            if (h > 0.01) view.dimensions.push_back({false, {0, 0}, {0, h}, {-1, 0}, 9.0, {0, 0}, h});
            std::map<std::array<std::int64_t, 3>, bool> seen;
            int count = 0;
            for (const auto& c : circles) {
                if (count >= 12) break;
                if (std::abs(c.axis.dot(eye)) < 0.999) continue;
                const Vec3 perp = c.axis.unitOrthogonal();
                const Vec3 onRim = c.centre + perp * c.radius;
                if (occ.hidden(onRim.dot(right), onRim.dot(up), onRim.dot(eye), gap)) continue;
                const Vec2 ctr = to2(c.centre);
                const std::array<std::int64_t, 3> key = {Quant::key(ctr.x()), Quant::key(ctr.y()), Quant::key(c.radius)};
                if (!seen.emplace(key, true).second) continue;
                view.dimensions.push_back({true, {0, 0}, {0, 0}, {0, 0}, 0, ctr, 2 * c.radius});
                const double ext = c.radius * 1.3;
                view.segments.push_back({ctr + Vec2(-ext, 0), ctr + Vec2(ext, 0), false, true});
                view.segments.push_back({ctr + Vec2(0, -ext), ctr + Vec2(0, ext), false, true});
                ++count;
            }
        }
        d.views.push_back(std::move(view));
    }

    // Sheet, layout and scale.
    const std::map<std::string, std::pair<double, double>> sheets = {{"A4", {297, 210}}, {"A3", {420, 297}}, {"A2", {594, 420}}};
    const auto sh = sheets.find(opt.sheet);
    if (sh == sheets.end()) throw SolidError("unknown sheet '" + opt.sheet + "' (A4, A3, A2)");
    d.sheetW = sh->second.first;
    d.sheetH = sh->second.second;
    auto where = [](const std::string& n) -> std::pair<int, int> {  // column, row (0: top, 1: middle, 2: bottom)
        if (n == "left") return {0, 1};
        if (n == "front") return {1, 1};
        if (n == "right") return {2, 1};
        if (n == "back") return {3, 1};
        if (n == "top") return {1, 0};
        return {1, 2};  // bottom
    };
    const double gapPaper = 26.0, margin = 18.0, titleBlock = 12.0;
    const double availW = d.sheetW - 2 * margin, availH = d.sheetH - 2 * margin - titleBlock;
    auto fits = [&](double s, std::array<double, 4>& colW, std::array<double, 3>& rowH) {
        colW.fill(0);
        rowH.fill(0);
        for (const auto& v : d.views) {
            const auto [c, r] = where(v.name);
            colW[static_cast<std::size_t>(c)] = std::max(colW[static_cast<std::size_t>(c)], v.size.x() * s);
            rowH[static_cast<std::size_t>(r)] = std::max(rowH[static_cast<std::size_t>(r)], v.size.y() * s);
        }
        auto total = [&](auto& a, std::size_t count) {
            double t = 0;
            int used = 0;
            for (std::size_t i = 0; i < count; ++i)
                if (a[i] > 0) { t += a[i]; ++used; }
            return t + gapPaper * std::max(0, used - 1);
        };
        return total(colW, 4) <= availW && total(rowH, 3) <= availH;
    };
    static const std::array<double, 15> ratios = {50, 20, 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001};
    std::array<double, 4> colW{};
    std::array<double, 3> rowH{};
    double scale = opt.scale;
    if (scale <= 0) {
        scale = ratios.back();
        for (double r : ratios)
            if (fits(r, colW, rowH)) { scale = r; break; }
    }
    fits(scale, colW, rowH);
    d.scale = scale;
    auto ratio = [](double s) {
        char b[48];
        if (s >= 1) std::snprintf(b, sizeof b, "%g:1", s);
        else std::snprintf(b, sizeof b, "1:%g", 1.0 / s);
        return std::string(b);
    };
    d.scaleText = ratio(scale);
    // Place the columns and rows (the middle row sits lowest of the top row, from the top margin).
    std::array<double, 4> colX{};
    double x = margin;
    {
        double total = 0;
        int used = 0;
        for (double w : colW) if (w > 0) { total += w; ++used; }
        total += gapPaper * std::max(0, used - 1);
        x = margin + std::max(0.0, (availW - total) / 2);
        for (std::size_t i = 0; i < 4; ++i) {
            colX[i] = x;
            if (colW[i] > 0) x += colW[i] + gapPaper;
        }
    }
    std::array<double, 3> rowY{};  // the lower edge of each row, from the sheet's bottom
    {
        double total = 0;
        int used = 0;
        for (double h : rowH) if (h > 0) { total += h; ++used; }
        total += gapPaper * std::max(0, used - 1);
        double y = margin + titleBlock + std::max(0.0, (availH - total) / 2) + total;  // the top of the stack
        for (std::size_t i = 0; i < 3; ++i) {
            if (rowH[i] > 0) { y -= rowH[i]; rowY[i] = y; y -= gapPaper; }
        }
    }
    for (auto& v : d.views) {
        const auto [c, r] = where(v.name);
        // Centre each view in its cell.
        v.origin = {colX[static_cast<std::size_t>(c)] + (colW[static_cast<std::size_t>(c)] - v.size.x() * scale) / 2,
                    rowY[static_cast<std::size_t>(r)] + (rowH[static_cast<std::size_t>(r)] - v.size.y() * scale) / 2};
    }
    return d;
}

void writeDrawing(const Drawing& d, const std::filesystem::path& path) {
    const std::vector<Prim> prims = layoutSheet(d);
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::ofstream os(path, std::ios::binary);
    if (!os) throw SolidError("cannot write " + path.string());
    if (ext == ".dxf") writeDxf(prims, d, os);
    else if (ext == ".svg") writeSvg(prims, d, os);
    else if (ext == ".pdf") writePdf(prims, d, os);
    else throw SolidError("use .pdf, .svg or .dxf");
    if (!os) throw SolidError("could not write " + path.string());
}

}  // namespace orv

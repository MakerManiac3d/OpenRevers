// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/SketchEditor.h"

#include "core/mesh/Distance.h"
#include "core/solid/BSpline.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace orv {

namespace {

constexpr double kPi = 3.14159265358979323846;

double cross2(const Vec2d& a, const Vec2d& b) { return a.x() * b.y() - a.y() * b.x(); }

/// The angle swept from a to b round the arc's centre, signed (+ ccw).
double sweepOf(const Segment& s) {
    const Vec2d u = s.a - s.centre, v = s.b - s.centre;
    double d = std::atan2(v.y(), v.x()) - std::atan2(u.y(), u.x());
    if (s.ccw) {
        while (d <= 0) d += 2 * kPi;
    } else {
        while (d >= 0) d -= 2 * kPi;
    }
    return d;
}

Vec2d arcMiddle(const Segment& s) {
    const Vec2d u = s.a - s.centre;
    const double a0 = std::atan2(u.y(), u.x()) + 0.5 * sweepOf(s);
    return s.centre + s.radius * Vec2d(std::cos(a0), std::sin(a0));
}

/// An arc from a to b through m (none when the three are in line).
std::optional<Segment> arcThrough(const Vec2d& a, const Vec2d& m, const Vec2d& b) {
    const double d = 2 * cross2(m - a, b - a);
    if (std::abs(d) < 1e-12 * (b - a).squaredNorm()) return std::nullopt;
    const Vec2d ma = m - a, ba = b - a;
    const Vec2d c = a + Vec2d(ba.y() * ma.squaredNorm() - ma.y() * ba.squaredNorm(), ma.x() * ba.squaredNorm() - ba.x() * ma.squaredNorm()) / d;
    return Segment::arc(a, b, c, cross2(m - a, b - m) > 0);
}

/// An arc whose ends moved: the centre on the new chord's bisector, on the
/// same side and at the same radius when the chord allows, so the arc keeps
/// its bulge.
void refitArc(Segment& s) {
    if (s.kind != Segment::Kind::Arc) return;
    const Vec2d chord = s.b - s.a;
    const double len = chord.norm();
    if (len < 1e-9) return;
    const Vec2d mid = 0.5 * (s.a + s.b), across = Vec2d(-chord.y(), chord.x()) / len;
    const double side = (s.centre - mid).dot(across) >= 0 ? 1.0 : -1.0;
    const double r = std::max(s.radius, 0.5 * len);
    const double h = std::sqrt(std::max(0.0, r * r - 0.25 * len * len));
    s.centre = mid + side * h * across;
    s.radius = (s.a - s.centre).norm();
}

double distToSegment(const Vec2d& q, const Segment& s) {
    if (s.kind == Segment::Kind::Line) {
        const Vec2d ab = s.b - s.a;
        const double t = std::clamp(ab.squaredNorm() > 0 ? (q - s.a).dot(ab) / ab.squaredNorm() : 0.0, 0.0, 1.0);
        return (q - (s.a + t * ab)).norm();
    }
    if (s.kind == Segment::Kind::Circle) return std::abs((q - s.centre).norm() - s.radius);
    if (s.kind == Segment::Kind::Spline) {
        const auto pts = s.sample(std::max(0.01, s.length() / 200));
        double best = 1e300;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec2d ab = pts[i + 1] - pts[i];
            const double t = std::clamp(ab.squaredNorm() > 0 ? (q - pts[i]).dot(ab) / ab.squaredNorm() : 0.0, 0.0, 1.0);
            best = std::min(best, (q - (pts[i] + t * ab)).norm());
        }
        return best;
    }
    const Vec2d u = s.a - s.centre, w = q - s.centre;
    double d = std::atan2(w.y(), w.x()) - std::atan2(u.y(), u.x());
    const double sw = sweepOf(s);
    if (sw > 0) {
        while (d < 0) d += 2 * kPi;
    } else {
        while (d > 0) d -= 2 * kPi;
    }
    if (std::abs(d) <= std::abs(sw)) return std::abs(w.norm() - s.radius);
    return std::min((q - s.a).norm(), (q - s.b).norm());
}

}  // namespace

// ============================================================== canvas ==

SketchCanvas::SketchCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(520, 420);
    setAutoFillBackground(true);
}

void SketchCanvas::setProfile(const Profile& p) {
    prof_ = p;
    pick_ = {};
    recomputeFit();
    update();
}

void SketchCanvas::setReference(std::vector<std::vector<Vec2d>> polylines) {
    reference_ = std::move(polylines);
    recomputeFit();
    update();
}

void SketchCanvas::setTolerance(double t) {
    tolerance_ = t;
    recomputeFit();
    update();
}

void SketchCanvas::setTool(Tool t) {
    tool_ = t;
    drawing_.clear();
    setCursor(t == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void SketchCanvas::setPick(const SketchPick& p) {
    pick_ = p;
    update();
    emit picked();
}

void SketchCanvas::fit() {
    Eigen::AlignedBox2d box;
    for (const auto& l : prof_.loops)
        for (const auto& q : l.polygon(0.1)) box.extend(q);
    if (box.isEmpty())
        for (const auto& pl : reference_)
            for (const auto& q : pl) box.extend(q);
    if (box.isEmpty()) box.extend(Vec2d(-10, -10)), box.extend(Vec2d(10, 10));
    centre_ = box.center();
    const Vec2d size = (box.max() - box.min()).cwiseMax(Vec2d(1, 1));
    scale_ = 0.85 * std::min(width() / size.x(), height() / size.y());
    update();
}

QPointF SketchCanvas::toScreen(const Vec2d& p) const {
    return {width() * 0.5 + (p.x() - centre_.x()) * scale_, height() * 0.5 - (p.y() - centre_.y()) * scale_};
}

Vec2d SketchCanvas::toPlane(const QPointF& p) const {
    return {centre_.x() + (p.x() - width() * 0.5) / scale_, centre_.y() - (p.y() - height() * 0.5) / scale_};
}

SketchPick SketchCanvas::hitTest(const QPointF& at) const {
    const double near = 7.0;  // pixels
    SketchPick best;
    double bestD = near;
    auto consider = [&](const Vec2d& p, SketchPick::What w, int l, int s, double bonus = 0, int index = -1) {
        const QPointF q = toScreen(p);
        const double d = std::hypot(q.x() - at.x(), q.y() - at.y()) - bonus;
        if (d < bestD) {
            bestD = d;
            best = {w, l, s, index};
        }
    };
    // Handles first (they sit on segments), then the segments themselves.
    for (int l = 0; l < static_cast<int>(prof_.loops.size()); ++l) {
        const auto& sg = prof_.loops[static_cast<std::size_t>(l)].segments;
        for (int s = 0; s < static_cast<int>(sg.size()); ++s) {
            const Segment& x = sg[static_cast<std::size_t>(s)];
            if (x.kind == Segment::Kind::Circle) {
                consider(x.centre, SketchPick::What::Centre, l, s, 2);
                consider(x.centre + Vec2d(x.radius, 0), SketchPick::What::Radius, l, s, 2);
                continue;
            }
            consider(x.a, SketchPick::What::Vertex, l, s, 2);
            if (x.kind == Segment::Kind::Arc) consider(arcMiddle(x), SketchPick::What::ArcMiddle, l, s, 1);
            if (x.kind == Segment::Kind::Spline)
                for (int k = 1; k + 1 < static_cast<int>(x.poles.size()); ++k) consider(x.poles[static_cast<std::size_t>(k)], SketchPick::What::Pole, l, s, 0, k);
        }
    }
    if (best.what != SketchPick::What::None) return best;
    const Vec2d p = toPlane(at);
    double bestMm = near / scale_;
    for (int l = 0; l < static_cast<int>(prof_.loops.size()); ++l) {
        const auto& sg = prof_.loops[static_cast<std::size_t>(l)].segments;
        for (int s = 0; s < static_cast<int>(sg.size()); ++s) {
            const double d = distToSegment(p, sg[static_cast<std::size_t>(s)]);
            if (d < bestMm) {
                bestMm = d;
                best = {SketchPick::What::Segment, l, s};
            }
        }
    }
    return best;
}

Vec2d SketchCanvas::snapped(const QPointF& at) const {
    const Vec2d p = toPlane(at);
    if (!snap_) return p;
    const double near = 8.0 / scale_;
    // Vertices and centres of the sketch first, then the section's points.
    double best = near;
    std::optional<Vec2d> to;
    for (int l = 0; l < static_cast<int>(prof_.loops.size()); ++l)
        for (int s = 0; s < static_cast<int>(prof_.loops[static_cast<std::size_t>(l)].segments.size()); ++s) {
            if (pick_.what == SketchPick::What::Vertex && dragging_ && pick_.loop == l && pick_.seg == s) continue;  // not onto itself
            const Segment& x = prof_.loops[static_cast<std::size_t>(l)].segments[static_cast<std::size_t>(s)];
            std::vector<Vec2d> marks;
            if (x.kind != Segment::Kind::Circle) marks.push_back(x.a);
            if (x.kind != Segment::Kind::Line) marks.push_back(x.centre);
            for (const Vec2d& c : marks) {
                const double d = (c - p).norm();
                if (d < best) { best = d; to = c; }
            }
        }
    if (to) return *to;
    for (const auto& pl : reference_)
        for (std::size_t k = 0; k + 1 < pl.size(); ++k) {
            const Vec2d ab = pl[k + 1] - pl[k];
            const double t = std::clamp(ab.squaredNorm() > 0 ? (p - pl[k]).dot(ab) / ab.squaredNorm() : 0.0, 0.0, 1.0);
            const Vec2d q = pl[k] + t * ab;
            const double d = (q - p).norm();
            if (d < best) { best = d; to = q; }
        }
    return to ? *to : p;
}

void SketchCanvas::pushUndo() {
    undo_.push_back(prof_);
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
}

void SketchCanvas::edited() {
    recomputeFit();
    update();
    emit changed();
}

void SketchCanvas::undo() {
    if (undo_.empty()) return;
    redo_.push_back(prof_);
    prof_ = undo_.back();
    undo_.pop_back();
    pick_ = {};
    edited();
    emit picked();
}

void SketchCanvas::redo() {
    if (redo_.empty()) return;
    undo_.push_back(prof_);
    prof_ = redo_.back();
    redo_.pop_back();
    pick_ = {};
    edited();
    emit picked();
}

void SketchCanvas::recomputeFit() {
    // Each section point goes to its nearest segment, if one is near enough
    // to be the outline it was drawn from.
    segOffset_.clear();
    std::size_t total = 0;
    for (const auto& l : prof_.loops) {
        segOffset_.push_back(static_cast<int>(total));
        total += l.segments.size();
    }
    segWorst_.assign(total, -1.0);
    std::vector<double> all;
    const double reach = std::max(1.0, 20 * tolerance_);
    for (const auto& pl : reference_)
        for (const auto& q : pl) {
            double best = reach;
            std::size_t at = total;
            for (std::size_t l = 0; l < prof_.loops.size(); ++l)
                for (std::size_t s = 0; s < prof_.loops[l].segments.size(); ++s) {
                    const double d = distToSegment(q, prof_.loops[l].segments[s]);
                    if (d < best) { best = d; at = static_cast<std::size_t>(segOffset_[l]) + s; }
                }
            if (at == total) continue;
            segWorst_[at] = std::max(segWorst_[at], best);
            all.push_back(best);
        }
    // And the other way: how far each segment strays from the section (an arc
    // bulging away from a straight outline has the section's points near its
    // ends only).
    if (!reference_.empty()) {
        std::size_t at = 0;
        for (const auto& l : prof_.loops)
            for (const auto& sg : l.segments) {
                double worst = 0;
                for (const Vec2d& q : sg.sample(std::max(0.05, sg.length() / 40))) {
                    double best = 1e300;
                    for (const auto& pl : reference_)
                        for (std::size_t k = 0; k + 1 < pl.size(); ++k) {
                            const Vec2d ab = pl[k + 1] - pl[k];
                            const double t = std::clamp(ab.squaredNorm() > 0 ? (q - pl[k]).dot(ab) / ab.squaredNorm() : 0.0, 0.0, 1.0);
                            best = std::min(best, (q - (pl[k] + t * ab)).squaredNorm());
                        }
                    worst = std::max(worst, std::sqrt(best));
                }
                segWorst_[at] = std::max(segWorst_[at], worst);
                ++at;
            }
    }
    fit_ = {};
    fit_.points = static_cast<int>(all.size());
    if (!all.empty()) {
        fit_.worst = *std::max_element(all.begin(), all.end());
        std::nth_element(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(all.size() / 2), all.end());
        fit_.median = all[all.size() / 2];
    }
}

// ---- edits ------------------------------------------------------------

void SketchCanvas::moveVertex(int l, int s, const Vec2d& to) {
    auto& sg = prof_.loops[static_cast<std::size_t>(l)].segments;
    const std::size_t i = static_cast<std::size_t>(s), prev = (i + sg.size() - 1) % sg.size();
    sg[i].a = to;
    sg[prev].b = to;
    if (sg[i].kind == Segment::Kind::Spline && !sg[i].poles.empty()) sg[i].poles.front() = to;
    if (sg[prev].kind == Segment::Kind::Spline && !sg[prev].poles.empty()) sg[prev].poles.back() = to;
    refitArc(sg[i]);
    refitArc(sg[prev]);
}

void SketchCanvas::movePole(int l, int s, int index, const Vec2d& to) {
    Segment& x = prof_.loops[static_cast<std::size_t>(l)].segments[static_cast<std::size_t>(s)];
    if (x.kind != Segment::Kind::Spline || index <= 0 || index + 1 >= static_cast<int>(x.poles.size())) return;
    x.poles[static_cast<std::size_t>(index)] = to;
}

void SketchCanvas::toSpline() {
    if (pick_.what != SketchPick::What::Segment && pick_.what != SketchPick::What::ArcMiddle) return;
    Segment& s = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments[static_cast<std::size_t>(pick_.seg)];
    if (s.kind != Segment::Kind::Line && s.kind != Segment::Kind::Arc) return;
    pushUndo();
    // Five poles along it: the same shape to start with, free to bend.
    const auto pts = s.sample(std::max(1e-3, s.length() / 64));
    s = Segment::spline(fitCurve(std::vector<Vec2d>(pts.begin() + 1, pts.end() - 1), s.a, s.b, 1e-4, 6).poles);
    pick_.what = SketchPick::What::Segment;
    edited();
    emit picked();
}

void SketchCanvas::lineToArc() {
    if (pick_.what != SketchPick::What::Segment) return;
    Segment& s = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments[static_cast<std::size_t>(pick_.seg)];
    if (s.kind != Segment::Kind::Line) return;
    pushUndo();
    // A gentle bulge outward (to the right of the chord in a counterclockwise loop).
    const Vec2d chord = s.b - s.a;
    const double side = prof_.loops[static_cast<std::size_t>(pick_.loop)].area() >= 0 ? -1.0 : 1.0;
    const Vec2d m = 0.5 * (s.a + s.b) + side * 0.15 * Vec2d(-chord.y(), chord.x());
    if (auto a = arcThrough(s.a, m, s.b)) s = *a;
    edited();
    emit picked();
}

void SketchCanvas::arcToLine() {
    if (pick_.what != SketchPick::What::Segment && pick_.what != SketchPick::What::ArcMiddle) return;
    Segment& s = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments[static_cast<std::size_t>(pick_.seg)];
    if (s.kind != Segment::Kind::Arc && s.kind != Segment::Kind::Spline) return;
    pushUndo();
    s = Segment::line(s.a, s.b);
    pick_.what = SketchPick::What::Segment;
    edited();
    emit picked();
}

void SketchCanvas::split() {
    if (pick_.what != SketchPick::What::Segment && pick_.what != SketchPick::What::ArcMiddle) return;
    auto& sg = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments;
    const std::size_t i = static_cast<std::size_t>(pick_.seg);
    const Segment s = sg[i];
    if (s.kind == Segment::Kind::Circle) return;
    pushUndo();
    if (s.kind == Segment::Kind::Line) {
        const Vec2d m = 0.5 * (s.a + s.b);
        sg[i] = Segment::line(s.a, m);
        sg.insert(sg.begin() + static_cast<std::ptrdiff_t>(i) + 1, Segment::line(m, s.b));
    } else if (s.kind == Segment::Kind::Spline) {
        // Each half refitted to its part of the curve.
        const auto pts = s.sample(std::max(1e-3, s.length() / 200));
        const std::size_t h = pts.size() / 2;
        const std::vector<Vec2d> first(pts.begin() + 1, pts.begin() + static_cast<std::ptrdiff_t>(h)), second(pts.begin() + static_cast<std::ptrdiff_t>(h) + 1, pts.end() - 1);
        const int n = std::max(4, static_cast<int>(s.poles.size()) / 2 + 2);
        sg[i] = Segment::spline(fitCurve(first, s.a, pts[h], 1e-4, n).poles);
        sg.insert(sg.begin() + static_cast<std::ptrdiff_t>(i) + 1, Segment::spline(fitCurve(second, pts[h], s.b, 1e-4, n).poles));
    } else {
        const Vec2d m = arcMiddle(s);
        sg[i] = Segment::arc(s.a, m, s.centre, s.ccw);
        sg.insert(sg.begin() + static_cast<std::ptrdiff_t>(i) + 1, Segment::arc(m, s.b, s.centre, s.ccw));
    }
    pick_ = {SketchPick::What::Vertex, pick_.loop, static_cast<int>(i) + 1};
    edited();
    emit picked();
}

void SketchCanvas::deleteVertex() {
    if (pick_.what != SketchPick::What::Vertex) return;
    auto& sg = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments;
    if (sg.size() <= 3) return;  // a loop needs three sides
    pushUndo();
    const std::size_t i = static_cast<std::size_t>(pick_.seg), prev = (i + sg.size() - 1) % sg.size();
    const Segment merged = Segment::line(sg[prev].a, sg[i].b);
    sg[prev] = merged;
    sg.erase(sg.begin() + static_cast<std::ptrdiff_t>(i));
    pick_ = {};
    edited();
    emit picked();
}

void SketchCanvas::deleteLoop() {
    if (pick_.loop < 0) return;
    pushUndo();
    prof_.loops.erase(prof_.loops.begin() + pick_.loop);
    pick_ = {};
    edited();
    emit picked();
}

void SketchCanvas::setArcRadius(double r) {
    if (pick_.loop < 0) return;
    Segment& s = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments[static_cast<std::size_t>(pick_.seg)];
    if (s.kind != Segment::Kind::Arc || r <= 0) return;
    pushUndo();
    s.radius = std::max(r, 0.5 * (s.b - s.a).norm() + 1e-9);
    refitArc(s);
    edited();
}

void SketchCanvas::setLineLength(double len) {
    if (pick_.loop < 0 || len <= 0) return;
    auto& sg = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments;
    const std::size_t i = static_cast<std::size_t>(pick_.seg);
    if (sg[i].kind != Segment::Kind::Line) return;
    pushUndo();
    const Vec2d dir = (sg[i].b - sg[i].a).normalized();
    moveVertex(pick_.loop, static_cast<int>((i + 1) % sg.size()), sg[i].a + len * dir);
    edited();
}

void SketchCanvas::setCircle(const Vec2d& c, double r) {
    if (pick_.loop < 0 || r <= 0) return;
    Segment& s = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments[static_cast<std::size_t>(pick_.seg)];
    if (s.kind != Segment::Kind::Circle) return;
    pushUndo();
    s = Segment::circle(c, r);
    edited();
}

void SketchCanvas::finishPolygon() {
    if (drawing_.size() >= 3) {
        pushUndo();
        Loop l;
        for (std::size_t k = 0; k < drawing_.size(); ++k) l.segments.push_back(Segment::line(drawing_[k], drawing_[(k + 1) % drawing_.size()]));
        prof_.loops.push_back(l);
        pick_ = {};
        edited();
    }
    drawing_.clear();
    update();
}

// ---- painting ---------------------------------------------------------

void SketchCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    p.fillRect(rect(), dark ? QColor(32, 34, 38) : QColor(250, 250, 252));

    // A grid of 1, 10 or 100 mm, whichever is about 40 px apart.
    double step = 1;
    while (step * scale_ < 25) step *= 10;
    while (step * scale_ > 250) step /= 10;
    const Vec2d lo = toPlane(QPointF(0, height())), hi = toPlane(QPointF(width(), 0));
    p.setPen(QPen(dark ? QColor(48, 52, 58) : QColor(232, 234, 238), 1));
    for (double x = std::floor(lo.x() / step) * step; x <= hi.x(); x += step) p.drawLine(toScreen({x, lo.y()}), toScreen({x, hi.y()}));
    for (double y = std::floor(lo.y() / step) * step; y <= hi.y(); y += step) p.drawLine(toScreen({lo.x(), y}), toScreen({hi.x(), y}));
    p.setPen(QPen(dark ? QColor(90, 60, 60) : QColor(230, 170, 170), 1));
    p.drawLine(toScreen({lo.x(), 0}), toScreen({hi.x(), 0}));
    p.setPen(QPen(dark ? QColor(60, 90, 60) : QColor(170, 215, 170), 1));
    p.drawLine(toScreen({0, lo.y()}), toScreen({0, hi.y()}));

    // The scan's section.
    p.setPen(QPen(dark ? QColor(140, 140, 150) : QColor(120, 120, 130), 1.2, Qt::DotLine));
    for (const auto& pl : reference_) {
        QPainterPath path;
        for (std::size_t k = 0; k < pl.size(); ++k) k ? path.lineTo(toScreen(pl[k])) : path.moveTo(toScreen(pl[k]));
        p.drawPath(path);
    }

    // The loops: each segment coloured by how far the section strays from it.
    auto colourOf = [&](std::size_t l, std::size_t s) {
        const double w = segOffset_.size() > l && static_cast<std::size_t>(segOffset_[l]) + s < segWorst_.size() ? segWorst_[static_cast<std::size_t>(segOffset_[l]) + s] : -1;
        if (w < 0) return dark ? QColor(120, 170, 255) : QColor(30, 90, 210);  // no section near it
        if (w <= tolerance_) return QColor(40, 160, 70);
        if (w <= 4 * tolerance_) return QColor(230, 140, 20);
        return QColor(220, 40, 40);
    };
    for (std::size_t l = 0; l < prof_.loops.size(); ++l)
        for (std::size_t s = 0; s < prof_.loops[l].segments.size(); ++s) {
            const Segment& x = prof_.loops[l].segments[s];
            const bool picked = pick_.loop == static_cast<int>(l) && pick_.seg == static_cast<int>(s) &&
                                (pick_.what == SketchPick::What::Segment || pick_.what == SketchPick::What::ArcMiddle || x.kind == Segment::Kind::Circle);
            const bool hovered = hover_.loop == static_cast<int>(l) && hover_.seg == static_cast<int>(s) && hover_.what == SketchPick::What::Segment;
            p.setPen(QPen(picked ? QColor(255, 150, 20) : colourOf(l, s), picked || hovered ? 3.2 : 2.0));
            QPainterPath path;
            const auto pts = x.sample(std::max(0.005, 2.0 / scale_));
            for (std::size_t k = 0; k < pts.size(); ++k) k ? path.lineTo(toScreen(pts[k])) : path.moveTo(toScreen(pts[k]));
            if (x.kind == Segment::Kind::Circle) path.closeSubpath();
            p.drawPath(path);
        }
    // Handles: vertices (squares), arc middles (diamonds), centres (crosses).
    for (std::size_t l = 0; l < prof_.loops.size(); ++l)
        for (std::size_t s = 0; s < prof_.loops[l].segments.size(); ++s) {
            const Segment& x = prof_.loops[l].segments[s];
            auto isPick = [&](SketchPick::What w) { return pick_.what == w && pick_.loop == static_cast<int>(l) && pick_.seg == static_cast<int>(s); };
            auto isHover = [&](SketchPick::What w) { return hover_.what == w && hover_.loop == static_cast<int>(l) && hover_.seg == static_cast<int>(s); };
            auto box = [&](const Vec2d& at, SketchPick::What w, bool diamond) {
                const QPointF q = toScreen(at);
                const double r = isPick(w) || isHover(w) ? 5.5 : 4.0;
                p.setPen(QPen(dark ? Qt::white : Qt::black, 1));
                p.setBrush(isPick(w) ? QColor(255, 150, 20) : (dark ? QColor(60, 64, 70) : Qt::white));
                if (diamond) {
                    QPolygonF d{{q.x(), q.y() - r}, {q.x() + r, q.y()}, {q.x(), q.y() + r}, {q.x() - r, q.y()}};
                    p.drawPolygon(d);
                } else {
                    p.drawRect(QRectF(q.x() - r, q.y() - r, 2 * r, 2 * r));
                }
            };
            if (x.kind == Segment::Kind::Circle) {
                box(x.centre, SketchPick::What::Centre, false);
                box(x.centre + Vec2d(x.radius, 0), SketchPick::What::Radius, true);
                continue;
            }
            box(x.a, SketchPick::What::Vertex, false);
            if (x.kind == Segment::Kind::Spline) {
                p.setPen(QPen(dark ? QColor(130, 110, 170) : QColor(170, 140, 210), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                for (std::size_t k = 0; k + 1 < x.poles.size(); ++k) p.drawLine(toScreen(x.poles[k]), toScreen(x.poles[k + 1]));
                for (int k = 1; k + 1 < static_cast<int>(x.poles.size()); ++k) {
                    const QPointF q = toScreen(x.poles[static_cast<std::size_t>(k)]);
                    const bool lit = pick_.what == SketchPick::What::Pole && pick_.loop == static_cast<int>(l) && pick_.seg == static_cast<int>(s) && pick_.index == k;
                    p.setPen(QPen(dark ? QColor(200, 180, 240) : QColor(120, 80, 170), 1));
                    p.setBrush(lit ? QColor(255, 150, 20) : (dark ? QColor(60, 50, 80) : QColor(240, 230, 255)));
                    p.drawEllipse(q, lit ? 5.0 : 3.5, lit ? 5.0 : 3.5);
                }
            }
            if (x.kind == Segment::Kind::Arc) {
                box(arcMiddle(x), SketchPick::What::ArcMiddle, true);
                const QPointF c = toScreen(x.centre);
                p.setPen(QPen(dark ? QColor(150, 150, 160) : QColor(140, 140, 150), 1));
                p.drawLine(c + QPointF(-4, 0), c + QPointF(4, 0));
                p.drawLine(c + QPointF(0, -4), c + QPointF(0, 4));
            }
        }
    p.setBrush(Qt::NoBrush);

    // The shape being drawn.
    if (!drawing_.empty() && cursor_) {
        p.setPen(QPen(QColor(255, 150, 20), 1.6, Qt::DashLine));
        if (tool_ == Tool::Rectangle) {
            const Vec2d a = drawing_.front(), b = *cursor_;
            p.drawPolygon(QPolygonF{toScreen(a), toScreen({b.x(), a.y()}), toScreen(b), toScreen({a.x(), b.y()})});
        } else if (tool_ == Tool::Circle) {
            const double r = (*cursor_ - drawing_.front()).norm() * scale_;
            p.drawEllipse(toScreen(drawing_.front()), r, r);
        } else {
            QPainterPath path;
            for (std::size_t k = 0; k < drawing_.size(); ++k) k ? path.lineTo(toScreen(drawing_[k])) : path.moveTo(toScreen(drawing_[k]));
            path.lineTo(toScreen(*cursor_));
            p.drawPath(path);
        }
    }
    // Scale bar.
    p.setPen(QPen(palette().color(QPalette::WindowText), 1));
    const double bar = step * scale_;
    p.drawLine(QPointF(12, height() - 14), QPointF(12 + bar, height() - 14));
    p.drawText(QPointF(16 + bar, height() - 10), QLocale().toString(step, 'g', 3) + " mm");
}

// ---- mouse ------------------------------------------------------------

void SketchCanvas::mousePressEvent(QMouseEvent* e) {
    lastMouse_ = e->position();
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
        panning_ = true;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const Vec2d at = snapped(e->position());
    switch (tool_) {
        case Tool::Select: {
            const SketchPick h = hitTest(e->position());
            pick_ = h;
            emit picked();
            if (h.what != SketchPick::What::None && h.what != SketchPick::What::Segment) {
                pushUndo();
                dragging_ = true;
            }
            update();
            return;
        }
        case Tool::Rectangle:
            if (drawing_.empty()) {
                drawing_.push_back(at);
            } else {
                const Vec2d a = drawing_.front(), b = at;
                drawing_.clear();
                if (std::abs(b.x() - a.x()) > 1e-6 && std::abs(b.y() - a.y()) > 1e-6) {
                    pushUndo();
                    Loop l;
                    const Vec2d c1(b.x(), a.y()), c3(a.x(), b.y());
                    l.segments = {Segment::line(a, c1), Segment::line(c1, b), Segment::line(b, c3), Segment::line(c3, a)};
                    prof_.loops.push_back(l);
                    edited();
                }
            }
            update();
            return;
        case Tool::Circle:
            if (drawing_.empty()) {
                drawing_.push_back(at);
            } else {
                const double r = (at - drawing_.front()).norm();
                const Vec2d c = drawing_.front();
                drawing_.clear();
                if (r > 1e-6) {
                    pushUndo();
                    Loop l;
                    l.segments = {Segment::circle(c, r)};
                    prof_.loops.push_back(l);
                    edited();
                }
            }
            update();
            return;
        case Tool::Polygon:
            // Clicking the first point again closes the polygon.
            if (drawing_.size() >= 3 && (toScreen(drawing_.front()) - e->position()).manhattanLength() < 9) {
                finishPolygon();
                return;
            }
            drawing_.push_back(at);
            update();
            return;
    }
}

void SketchCanvas::mouseMoveEvent(QMouseEvent* e) {
    const QPointF d = e->position() - lastMouse_;
    lastMouse_ = e->position();
    if (panning_) {
        centre_ -= Vec2d(d.x() / scale_, -d.y() / scale_);
        update();
        return;
    }
    const Vec2d at = snapped(e->position());
    cursor_ = at;
    emit cursorMoved(QPointF(at.x(), at.y()));
    if (dragging_ && pick_.loop >= 0) {
        auto& sg = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments;
        Segment& s = sg[static_cast<std::size_t>(pick_.seg)];
        switch (pick_.what) {
            case SketchPick::What::Vertex: moveVertex(pick_.loop, pick_.seg, at); break;
            case SketchPick::What::ArcMiddle:
                if (auto a = arcThrough(s.a, at, s.b)) s = *a;
                break;
            case SketchPick::What::Pole: movePole(pick_.loop, pick_.seg, pick_.index, at); break;
            case SketchPick::What::Centre: s.centre = at; break;
            case SketchPick::What::Radius: s.radius = std::max(1e-3, (at - s.centre).norm()); break;
            default: break;
        }
        edited();
        emit picked();
        return;
    }
    if (tool_ == Tool::Select) {
        const SketchPick h = hitTest(e->position());
        if (!(h == hover_)) {
            hover_ = h;
            update();
        }
    } else {
        update();
    }
}

void SketchCanvas::mouseReleaseEvent(QMouseEvent*) {
    if (dragging_) {
        // A drag that changed nothing leaves no step to undo.
        if (!undo_.empty()) {
            const Profile& before = undo_.back();
            bool same = before.loops.size() == prof_.loops.size();
            for (std::size_t l = 0; same && l < prof_.loops.size(); ++l) {
                same = before.loops[l].segments.size() == prof_.loops[l].segments.size();
                for (std::size_t s = 0; same && s < prof_.loops[l].segments.size(); ++s) {
                    const auto &x = before.loops[l].segments[s], &y = prof_.loops[l].segments[s];
                    same = x.a == y.a && x.b == y.b && x.centre == y.centre && x.radius == y.radius && x.ccw == y.ccw && x.kind == y.kind && x.poles == y.poles;
                }
            }
            if (same) undo_.pop_back();
        }
    }
    dragging_ = false;
    panning_ = false;
}

void SketchCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (tool_ == Tool::Polygon) {
        finishPolygon();
        return;
    }
    // On a line: make it an arc; on an arc: a line again.
    const SketchPick h = hitTest(e->position());
    if (h.what == SketchPick::What::Segment) {
        pick_ = h;
        const Segment& s = prof_.loops[static_cast<std::size_t>(h.loop)].segments[static_cast<std::size_t>(h.seg)];
        if (s.kind == Segment::Kind::Line) lineToArc();
        else if (s.kind == Segment::Kind::Arc) arcToLine();
    }
}

void SketchCanvas::wheelEvent(QWheelEvent* e) {
    // Zoom about the point under the cursor.
    const Vec2d before = toPlane(e->position());
    scale_ = std::clamp(scale_ * std::pow(1.15, e->angleDelta().y() / 120.0), 0.05, 5000.0);
    const Vec2d after = toPlane(e->position());
    centre_ += before - after;
    update();
}

void SketchCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape && !drawing_.empty()) {
        drawing_.clear();
        update();
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && tool_ == Tool::Polygon) {
        finishPolygon();
        return;
    }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        if (pick_.what == SketchPick::What::Vertex) deleteVertex();
        else if (pick_.what == SketchPick::What::Centre || pick_.what == SketchPick::What::Radius) deleteLoop();
        return;
    }
    if (e->key() == Qt::Key_F) {
        fit();
        return;
    }
    QWidget::keyPressEvent(e);
}

void SketchCanvas::resizeEvent(QResizeEvent*) { update(); }

// ============================================================== dialog ==

SketchEditor::SketchEditor(const Profile& profile, const QString& name, const std::vector<Body>& bodies, const QString& lang, QWidget* parent)
    : QDialog(parent), bodies_(bodies) {
    (void)lang;
    setWindowTitle(tr("Sketch: %1").arg(name));
    resize(1100, 720);
    canvas_ = new SketchCanvas(this);
    canvas_->setProfile(profile);

    auto* tb = new QToolBar(this);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* tools = new QActionGroup(this);
    auto tool = [&](const QString& text, SketchCanvas::Tool t, const QString& tip) {
        QAction* a = tb->addAction(text, this, [this, t]() { canvas_->setTool(t); });
        a->setCheckable(true);
        a->setToolTip(tip);
        tools->addAction(a);
        return a;
    };
    tool(tr("Select"), SketchCanvas::Tool::Select, tr("Pick and drag vertices, arc middles, centres and radii. Double-click a line to make it an arc, an arc to make it a line."))->setChecked(true);
    tool(tr("Rectangle"), SketchCanvas::Tool::Rectangle, tr("Two clicks: opposite corners"));
    tool(tr("Circle"), SketchCanvas::Tool::Circle, tr("Two clicks: centre, then a point on the circle"));
    tool(tr("Polygon"), SketchCanvas::Tool::Polygon, tr("Click the corners; double-click, Enter or a click on the first corner closes it"));
    tb->addSeparator();
    tb->addAction(tr("Line ↔ arc"), this, [this]() {
        const auto& p = canvas_->pick();
        if (p.loop < 0) return;
        const Segment& s = canvas_->profile().loops[static_cast<std::size_t>(p.loop)].segments[static_cast<std::size_t>(p.seg)];
        if (s.kind == Segment::Kind::Line) canvas_->lineToArc();
        else canvas_->arcToLine();
    })->setToolTip(tr("Turn the picked line into an arc, or the picked arc or spline into a line"));
    tb->addAction(tr("To spline"), this, [this]() { canvas_->toSpline(); })->setToolTip(tr("Turn the picked line or arc into a spline, to bend it freely by its poles"));
    tb->addAction(tr("Split"), this, [this]() { canvas_->split(); })->setToolTip(tr("Split the picked segment in two at its middle"));
    tb->addAction(tr("Delete vertex"), this, [this]() { canvas_->deleteVertex(); })->setToolTip(tr("Join the two segments at the picked vertex into one line (Del)"));
    tb->addAction(tr("Delete loop"), this, [this]() { canvas_->deleteLoop(); })->setToolTip(tr("Delete the whole loop the pick is on"));
    tb->addSeparator();
    tb->addAction(tr("Undo"), this, [this]() { canvas_->undo(); })->setShortcut(QKeySequence::Undo);
    tb->addAction(tr("Redo"), this, [this]() { canvas_->redo(); })->setShortcut(QKeySequence::Redo);
    tb->addAction(tr("Fit"), this, [this]() { canvas_->fit(); })->setToolTip(tr("Fit the sketch in the window (F)"));

    // Side panel: the reference, the tolerance, the picked element's values.
    scan_ = new QComboBox();
    scan_->addItem(tr("(none)"), QVariant::fromValue<qulonglong>(0));
    int firstScan = 0;
    for (const auto& b : bodies_)
        if (!b.isPart() && !b.mesh->faces.empty()) {
            scan_->addItem(QString::fromStdString(b.name), QVariant::fromValue<qulonglong>(b.id));
            if (!firstScan && b.visible) firstScan = scan_->count() - 1;
        }
    scan_->setCurrentIndex(firstScan);
    tol_ = new QDoubleSpinBox();
    tol_->setDecimals(3);
    tol_->setRange(0.001, 10);
    tol_->setSingleStep(0.01);
    tol_->setValue(0.05);
    tol_->setSuffix(" mm");
    snap_ = new QCheckBox(tr("Snap to vertices and to the section"));
    snap_->setChecked(true);
    formBox_ = new QWidget();
    form_ = new QFormLayout(formBox_);
    status_ = new QLabel();
    status_->setWordWrap(true);
    cursor_ = new QLabel();

    auto* side = new QWidget();
    side->setMinimumWidth(290);
    auto* sl = new QVBoxLayout(side);
    auto* top = new QFormLayout();
    top->addRow(tr("Scan section:"), scan_);
    top->addRow(tr("Tolerance:"), tol_);
    sl->addLayout(top);
    sl->addWidget(snap_);
    auto* legend = new QLabel(tr("<span style='color:#28a046'>■</span> within the tolerance &nbsp; <span style='color:#e68c14'>■</span> up to 4× &nbsp; "
                                 "<span style='color:#dc2828'>■</span> more"));
    legend->setWordWrap(true);
    sl->addWidget(legend);
    sl->addSpacing(8);
    sl->addWidget(new QLabel(tr("<b>Picked</b>")));
    sl->addWidget(formBox_);
    sl->addStretch(1);
    sl->addWidget(status_);
    sl->addWidget(cursor_);

    auto* hl = new QHBoxLayout();
    hl->addWidget(canvas_, 1);
    hl->addWidget(side);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* vl = new QVBoxLayout(this);
    vl->addWidget(tb);
    vl->addLayout(hl, 1);
    vl->addWidget(buttons_);

    connect(scan_, &QComboBox::currentIndexChanged, this, [this]() { loadReference(); });
    connect(tol_, &QDoubleSpinBox::valueChanged, this, [this](double v) { canvas_->setTolerance(v); updateStatus(); });
    connect(snap_, &QCheckBox::toggled, this, [this](bool on) { canvas_->setSnap(on); });
    connect(canvas_, &SketchCanvas::changed, this, [this]() { updateStatus(); });
    // Queued: the panel's own boxes send edits that change the pick, and must not be deleted while they do.
    connect(canvas_, &SketchCanvas::picked, this, [this]() { updatePanel(); }, Qt::QueuedConnection);
    connect(canvas_, &SketchCanvas::cursorMoved, this, [this](const QPointF& p) {
        cursor_->setText(QString("x %1  y %2 mm").arg(QLocale().toString(p.x(), 'f', 3), QLocale().toString(p.y(), 'f', 3)));
    });
    canvas_->setTolerance(tol_->value());
    loadReference();
    updatePanel();
    updateStatus();
    QMetaObject::invokeMethod(canvas_, [this]() { canvas_->fit(); }, Qt::QueuedConnection);
}

Profile SketchEditor::profile() const { return canvas_->profile(); }

void SketchEditor::loadReference() {
    std::vector<std::vector<Vec2d>> lines;
    const auto id = scan_->currentData().toULongLong();
    SketchPlane pl = canvas_->profile().plane;
    pl.normalize();
    for (const auto& b : bodies_)
        if (b.id == id && b.mesh && !b.mesh->faces.empty())
            for (const auto& s : sectionMesh(*b.mesh, pl.origin, pl.normal, pl.xDir)) {
                auto pts = s.points;
                if (s.closed && !pts.empty()) pts.push_back(pts.front());
                lines.push_back(std::move(pts));
            }
    canvas_->setReference(std::move(lines));
    updateStatus();
}

void SketchEditor::updateStatus() {
    QLocale loc;
    QStringList lines;
    const std::string why = canvas_->profile().problem(1e-6);
    const bool empty = canvas_->profile().loops.empty();
    if (empty) lines << tr("Draw a loop: a rectangle, a circle or a polygon.");
    else if (!why.empty()) lines << "<span style='color:#dc2828'>" + tr("Not usable yet: %1").arg(QString::fromStdString(why)) + "</span>";
    else {
        std::size_t segs = 0;
        for (const auto& l : canvas_->profile().loops) segs += l.segments.size();
        lines << tr("%1 loops, %2 segments.").arg(canvas_->profile().loops.size()).arg(segs);
    }
    const auto f = canvas_->fitToReference();
    if (f.points > 0)
        lines << tr("Section from the sketch: worst %1 mm, median %2 mm.").arg(loc.toString(f.worst, 'f', 3), loc.toString(f.median, 'f', 3));
    status_->setText(lines.join("<br>"));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!empty && why.empty());
}

void SketchEditor::updatePanel() {
    // The picked element's values, ready to type over.
    while (form_->rowCount() > 0) form_->removeRow(0);
    const SketchPick& p = canvas_->pick();
    if (p.loop < 0 || p.what == SketchPick::What::None) {
        form_->addRow(new QLabel(tr("Nothing. Click a vertex, a segment or a circle.")));
        return;
    }
    const auto& loop = canvas_->profile().loops[static_cast<std::size_t>(p.loop)];
    const Segment s = loop.segments[static_cast<std::size_t>(p.seg)];
    auto spin = [&](double v, double lo = -1e5, double hi = 1e5) {
        auto* b = new QDoubleSpinBox();
        b->setDecimals(4);
        b->setRange(lo, hi);
        b->setSingleStep(0.01);
        b->setValue(v);
        b->setSuffix(" mm");
        b->setKeyboardTracking(false);
        return b;
    };
    if (s.kind == Segment::Kind::Circle) {
        auto *x = spin(s.centre.x()), *y = spin(s.centre.y()), *d = spin(2 * s.radius, 0.001);
        form_->addRow(tr("Circle centre x"), x);
        form_->addRow(tr("Circle centre y"), y);
        form_->addRow(tr("Diameter"), d);
        auto apply = [this, x, y, d]() { canvas_->setCircle(Vec2d(x->value(), y->value()), d->value() / 2); };
        for (auto* b : {x, y, d}) connect(b, &QDoubleSpinBox::valueChanged, this, apply);
        return;
    }
    if (p.what == SketchPick::What::Pole) {
        const Vec2d q = s.poles[static_cast<std::size_t>(p.index)];
        auto *x = spin(q.x()), *y = spin(q.y());
        form_->addRow(tr("Pole x"), x);
        form_->addRow(tr("Pole y"), y);
        const int l = p.loop, sg = p.seg, k = p.index;
        auto apply = [this, x, y, l, sg, k]() {
            canvas_->movePole(l, sg, k, Vec2d(x->value(), y->value()));
            canvas_->setPick({SketchPick::What::Pole, l, sg, k});
            emit canvas_->changed();
        };
        for (auto* b : {x, y}) connect(b, &QDoubleSpinBox::editingFinished, this, apply);
        return;
    }
    if (p.what == SketchPick::What::Vertex) {
        auto *x = spin(s.a.x()), *y = spin(s.a.y());
        form_->addRow(tr("Vertex x"), x);
        form_->addRow(tr("Vertex y"), y);
        const int l = p.loop, sg = p.seg;
        auto apply = [this, x, y, l, sg]() {
            canvas_->moveVertex(l, sg, Vec2d(x->value(), y->value()));
            canvas_->setPick({SketchPick::What::Vertex, l, sg});
            emit canvas_->changed();
        };
        for (auto* b : {x, y}) connect(b, &QDoubleSpinBox::editingFinished, this, apply);
        return;
    }
    QLocale loc;
    form_->addRow(tr("From"), new QLabel(loc.toString(s.a.x(), 'f', 4) + "; " + loc.toString(s.a.y(), 'f', 4)));
    form_->addRow(tr("To"), new QLabel(loc.toString(s.b.x(), 'f', 4) + "; " + loc.toString(s.b.y(), 'f', 4)));
    if (s.kind == Segment::Kind::Spline) {
        form_->addRow(tr("Spline"), new QLabel(tr("%1 poles, %2 mm long").arg(s.poles.size()).arg(loc.toString(s.length(), 'f', 3))));
        form_->addRow(new QLabel(tr("Drag its poles (circles) to bend it.")));
        return;
    }
    if (s.kind == Segment::Kind::Line) {
        auto* len = spin(s.length(), 0.001);
        form_->addRow(tr("Length"), len);
        connect(len, &QDoubleSpinBox::editingFinished, this, [this, len]() { canvas_->setLineLength(len->value()); });
        const double ang = std::atan2(s.b.y() - s.a.y(), s.b.x() - s.a.x()) * 180 / kPi;
        form_->addRow(tr("Angle"), new QLabel(loc.toString(ang, 'f', 3) + "°"));
    } else {
        auto* r = spin(s.radius, 0.001);
        form_->addRow(tr("Radius"), r);
        connect(r, &QDoubleSpinBox::editingFinished, this, [this, r]() { canvas_->setArcRadius(r->value()); });
        form_->addRow(tr("Centre"), new QLabel(loc.toString(s.centre.x(), 'f', 4) + "; " + loc.toString(s.centre.y(), 'f', 4)));
        form_->addRow(tr("Angle"), new QLabel(loc.toString(std::abs(sweepOf(s)) * 180 / kPi, 'f', 2) + "°"));
    }
}

}  // namespace orv

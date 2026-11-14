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
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QListWidget>
#include <QMessageBox>
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
    selection_.clear();
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
    selection_.clear();
    if (p.what != SketchPick::What::None) selection_.push_back(p);
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
    const double reachPx = 7.0;  // pixels
    SketchPick best;
    double bestD = reachPx;
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
    double bestMm = reachPx / scale_;
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
    const double reachMm = 8.0 / scale_;
    // Vertices and centres of the sketch first, then the section's points.
    double best = reachMm;
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
    undo_.push_back({prof_, cons_});
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
}

void SketchCanvas::edited() {
    settle();
    recomputeFit();
    update();
    emit changed();
}

void SketchCanvas::undo() {
    if (undo_.empty()) return;
    redo_.push_back({prof_, cons_});
    prof_ = undo_.back().profile;
    cons_ = undo_.back().constraints;
    undo_.pop_back();
    pickedCons_ = -1;
    emit constraintsChanged();
    pick_ = {};
    selection_.clear();
    edited();
    emit picked();
}

void SketchCanvas::redo() {
    if (redo_.empty()) return;
    undo_.push_back({prof_, cons_});
    prof_ = redo_.back().profile;
    cons_ = redo_.back().constraints;
    redo_.pop_back();
    pickedCons_ = -1;
    emit constraintsChanged();
    pick_ = {};
    selection_.clear();
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
    dropOnSegment(pick_.loop, pick_.seg);
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
    dropOnSegment(pick_.loop, pick_.seg);
    edited();
    emit picked();
}

void SketchCanvas::arcToLine() {
    if (pick_.what != SketchPick::What::Segment && pick_.what != SketchPick::What::ArcMiddle) return;
    Segment& s = prof_.loops[static_cast<std::size_t>(pick_.loop)].segments[static_cast<std::size_t>(pick_.seg)];
    if (s.kind != Segment::Kind::Arc && s.kind != Segment::Kind::Spline) return;
    pushUndo();
    s = Segment::line(s.a, s.b);
    dropOnSegment(pick_.loop, pick_.seg);
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
    afterSplit(pick_.loop, static_cast<int>(i));
    pick_ = {SketchPick::What::Vertex, pick_.loop, static_cast<int>(i) + 1};
    selection_ = {pick_};
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
    dropOnLoop(pick_.loop, false);
    pick_ = {};
    selection_.clear();
    edited();
    emit picked();
}

void SketchCanvas::deleteLoop() {
    if (pick_.loop < 0) return;
    pushUndo();
    prof_.loops.erase(prof_.loops.begin() + pick_.loop);
    dropOnLoop(pick_.loop, true);
    pick_ = {};
    selection_.clear();
    edited();
    emit picked();
}

void SketchCanvas::toggleConstruction() {
    if (pick_.loop < 0 || pick_.loop >= static_cast<int>(prof_.loops.size())) return;
    pushUndo();
    Loop& l = prof_.loops[static_cast<std::size_t>(pick_.loop)];
    l.construction = !l.construction;
    edited();
    emit picked();
}

void SketchCanvas::selectReference(int which) {
    const SketchPick p = which == 0 ? SketchPick{SketchPick::What::Vertex, -1, 0} : SketchPick{SketchPick::What::Segment, -1, which == 1 ? 0 : 1};
    if (!selected(p)) selection_.push_back(p);
    update();
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
        addAutoConstraints(static_cast<int>(prof_.loops.size()) - 1);
        pick_ = {};
    selection_.clear();
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
            bool picked = false;
            for (const auto& q : selection_)
                picked = picked || (q.loop == static_cast<int>(l) && q.seg == static_cast<int>(s) &&
                                    (q.what == SketchPick::What::Segment || q.what == SketchPick::What::ArcMiddle || x.kind == Segment::Kind::Circle));
            const bool hovered = hover_.loop == static_cast<int>(l) && hover_.seg == static_cast<int>(s) && hover_.what == SketchPick::What::Segment;
            QPen pen(picked ? QColor(255, 150, 20) : (prof_.loops[l].construction ? (dark ? QColor(110, 200, 190) : QColor(0, 140, 130)) : colourOf(l, s)),
                     picked || hovered ? 3.2 : 2.0);
            if (prof_.loops[l].construction) pen.setStyle(Qt::DashLine);
            p.setPen(pen);
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
            auto isPick = [&](SketchPick::What w) { return selected({w, static_cast<int>(l), static_cast<int>(s)}); };
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

    // The origin: lit when picked for a constraint.
    if (selected({SketchPick::What::Vertex, -1, 0}) || selected({SketchPick::What::Segment, -1, 0}) || selected({SketchPick::What::Segment, -1, 1})) {
        p.setPen(QPen(QColor(255, 150, 20), 2));
        const QPointF o = toScreen({0, 0});
        p.drawEllipse(o, 7.0, 7.0);
        if (selected({SketchPick::What::Segment, -1, 0})) p.drawLine(toScreen({-1e4, 0}), toScreen({1e4, 0}));
        if (selected({SketchPick::What::Segment, -1, 1})) p.drawLine(toScreen({0, -1e4}), toScreen({0, 1e4}));
    }

    // Constraints: a tag beside what each holds; the picked one's pieces lit.
    tagRects_.clear();
    if (pickedCons_ >= 0 && pickedCons_ < static_cast<int>(cons_.size())) {
        p.setPen(QPen(QColor(200, 60, 200), 4));
        for (const auto& r : cons_[static_cast<std::size_t>(pickedCons_)].refs) {
            if (r.loop == -1) {
                p.drawEllipse(toScreen({0, 0}), 6.0, 6.0);
                if (r.part == SketchRef::Part::Edge) p.drawLine(r.segment == 1 ? toScreen({0, -1e4}) : toScreen({-1e4, 0}), r.segment == 1 ? toScreen({0, 1e4}) : toScreen({1e4, 0}));
                continue;
            }
            if (r.loop < 0 || r.loop >= static_cast<int>(prof_.loops.size())) continue;
            const auto& sg = prof_.loops[static_cast<std::size_t>(r.loop)].segments;
            if (r.segment < 0 || r.segment >= static_cast<int>(sg.size())) continue;
            if (r.part == SketchRef::Part::Edge) {
                QPainterPath path;
                const auto pts = sg[static_cast<std::size_t>(r.segment)].sample(std::max(0.005, 2.0 / scale_));
                for (std::size_t k = 0; k < pts.size(); ++k) k ? path.lineTo(toScreen(pts[k])) : path.moveTo(toScreen(pts[k]));
                p.drawPath(path);
            } else {
                p.drawEllipse(toScreen(anchorOf(r)), 6.0, 6.0);
            }
        }
    }
    {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        p.setFont(f);
        const QFontMetricsF fm(f);
        std::vector<QPointF> used;
        for (std::size_t i = 0; i < cons_.size(); ++i) {
            const auto& c = cons_[i];
            if (c.refs.empty()) continue;
            bool valid = true;
            for (const auto& r : c.refs)
                valid = valid && (r.loop == -1 ? r.segment >= 0 && r.segment <= 1
                                               : r.loop >= 0 && r.loop < static_cast<int>(prof_.loops.size()) && r.segment >= 0 &&
                                                     r.segment < static_cast<int>(prof_.loops[static_cast<std::size_t>(r.loop)].segments.size()));
            if (!valid) continue;
            Vec2d at = anchorOf(c.refs.front());
            if (constraintHasValue(c.kind) && c.refs.size() >= 2) at = 0.5 * (anchorOf(c.refs[0]) + anchorOf(c.refs[1]));
            QPointF q = toScreen(at) + QPointF(8, -8);
            // Tags on the same spot stack up.
            for (const QPointF& u : used)
                if ((u - q).manhattanLength() < 6) q += QPointF(0, -fm.height() - 2);
            used.push_back(q);
            const QString text = labelOf(c);
            QRectF box(q, QSizeF(fm.horizontalAdvance(text) + 8, fm.height() + 2));
            box.translate(0, -box.height());
            const bool lit = static_cast<int>(i) == pickedCons_;
            const bool dim = constraintHasValue(c.kind);
            p.setPen(QPen(lit ? QColor(200, 60, 200) : (dim ? QColor(30, 110, 200) : QColor(120, 120, 130)), 1));
            p.setBrush(dark ? QColor(44, 46, 52, 230) : QColor(255, 255, 255, 230));
            p.drawRoundedRect(box, 3, 3);
            p.setPen(lit ? QColor(200, 60, 200) : (dim ? (dark ? QColor(130, 180, 255) : QColor(20, 80, 180)) : palette().color(QPalette::WindowText)));
            p.drawText(box, Qt::AlignCenter, text);
            tagRects_.push_back({box, static_cast<int>(i)});
        }
        p.setBrush(Qt::NoBrush);
        p.setFont(font());
    }

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
            if (const int c = constraintAt(e->position()); c >= 0) {
                setPickedConstraint(c);
                emit constraintPicked(c);
                return;
            }
            const SketchPick h = hitTest(e->position());
            const bool add = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
            if (add) {
                if (h.what != SketchPick::What::None && !selected(h)) selection_.push_back(h);
                if (selection_.size() > 3) selection_.erase(selection_.begin());
            } else {
                selection_.clear();
                if (h.what != SketchPick::What::None) selection_.push_back(h);
            }
            if (pickedCons_ >= 0) {
                pickedCons_ = -1;
                emit constraintPicked(-1);
            }
            if (!add || h.what != SketchPick::What::None) pick_ = h;
            emit picked();
            if (add) {
                update();
                return;
            }
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
                    addAutoConstraints(static_cast<int>(prof_.loops.size()) - 1);
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
        const Profile before = prof_;
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
        if (!cons_.empty()) {
            // Solved live: the dragged point follows the cursor as far as the
            // constraints let it; a move they cannot follow is not made.
            SketchSolveOptions opt;
            if (pick_.what == SketchPick::What::Vertex) opt.drag.push_back({SketchRef{pick_.loop, pick_.seg, SketchRef::Part::Start}, at});
            if (pick_.what == SketchPick::What::Centre) opt.drag.push_back({SketchRef{pick_.loop, pick_.seg, SketchRef::Part::Centre}, at});
            Profile cand = prof_;
            int dof = -1;
            bool ok = solveOn(cand, opt, nullptr, &dof);
            if (!ok && !opt.drag.empty()) {
                cand = prof_;
                ok = solveOn(cand, {}, nullptr, &dof);
            }
            if (ok) {
                prof_ = cand;
                dof_ = dof;
            } else {
                prof_ = before;
            }
            recomputeFit();
            update();
            emit changed();
            emit picked();
            return;
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
            const Profile& before = undo_.back().profile;
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
    if (const int c = constraintAt(e->position()); c >= 0 && tool_ == Tool::Select) {
        emit constraintEditRequested(c);
        return;
    }
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
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && pickedCons_ >= 0) {
        removeConstraint(pickedCons_);
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

// ---- constraints ------------------------------------------------------

void SketchCanvas::setConstraints(std::vector<SketchConstraint> c, std::map<std::string, double> parameters) {
    cons_ = std::move(c);
    params_ = std::move(parameters);
    pickedCons_ = -1;
    settle();
    recomputeFit();
    update();
    emit constraintsChanged();
}

bool SketchCanvas::solveOn(Profile& p, const SketchSolveOptions& opt, QString* why, int* dof) const {
    std::vector<SketchConstraint> cs = cons_;
    for (auto& c : cs)
        if (!c.parameter.empty()) {
            const auto it = params_.find(c.parameter);
            if (it == params_.end()) {
                if (why) *why = tr("there is no parameter named %1").arg(QString::fromStdString(c.parameter));
                return false;
            }
            c.value = it->second;
        }
    const SketchSolveResult r = solveSketch(p, cs, opt);
    if (dof) *dof = r.dof;
    if (r.solved) return true;
    if (why) {
        QStringList names;
        for (int i : r.conflicting)
            if (i >= 0 && i < static_cast<int>(cons_.size())) names << QString("%1 #%2").arg(kindTitle(cons_[static_cast<std::size_t>(i)].kind)).arg(i + 1);
        *why = r.conflicting.empty() ? QString::fromStdString(r.message) : tr("they contradict each other: %1").arg(names.join(", "));
        if (why->isEmpty()) *why = tr("the solver found no solution");
    }
    return false;
}

void SketchCanvas::settle() {
    if (cons_.empty()) {
        dof_ = -1;
        solved_ = true;
        solveMsg_.clear();
        return;
    }
    Profile p = prof_;
    QString why;
    int dof = -1;
    if (solveOn(p, {}, &why, &dof)) {
        prof_ = p;
        dof_ = dof;
        solved_ = true;
        solveMsg_.clear();
    } else {
        solved_ = false;
        solveMsg_ = why;
    }
}

QString SketchCanvas::addConstraint(const SketchConstraint& c) {
    const auto before = cons_;
    cons_.push_back(c);
    Profile p = prof_;
    QString why;
    int dof = -1;
    const bool ok = solveOn(p, {}, &why, &dof);
    cons_ = before;
    if (!ok) return why;
    pushUndo();
    cons_.push_back(c);
    prof_ = p;
    dof_ = dof;
    solved_ = true;
    solveMsg_.clear();
    pickedCons_ = static_cast<int>(cons_.size()) - 1;
    recomputeFit();
    update();
    emit changed();
    emit constraintsChanged();
    emit constraintPicked(pickedCons_);
    return {};
}

void SketchCanvas::removeConstraint(int index) {
    if (index < 0 || index >= static_cast<int>(cons_.size())) return;
    pushUndo();
    cons_.erase(cons_.begin() + index);
    pickedCons_ = -1;
    edited();
    emit constraintsChanged();
    emit constraintPicked(-1);
}

QString SketchCanvas::setConstraintValue(int index, double value, const std::string& parameter) {
    if (index < 0 || index >= static_cast<int>(cons_.size())) return {};
    const auto before = cons_;
    cons_[static_cast<std::size_t>(index)].value = value;
    cons_[static_cast<std::size_t>(index)].parameter = parameter;
    Profile p = prof_;
    QString why;
    int dof = -1;
    const bool ok = solveOn(p, {}, &why, &dof);
    const auto after = cons_;
    cons_ = before;
    if (!ok) return why;
    pushUndo();
    cons_ = after;
    prof_ = p;
    dof_ = dof;
    solved_ = true;
    solveMsg_.clear();
    recomputeFit();
    update();
    emit changed();
    emit constraintsChanged();
    return {};
}

void SketchCanvas::setPickedConstraint(int index) {
    pickedCons_ = index >= 0 && index < static_cast<int>(cons_.size()) ? index : -1;
    update();
}

std::optional<double> SketchCanvas::measure(const SketchConstraint& c) const {
    try {
        return measureConstraint(prof_, c);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<SketchRef> SketchCanvas::selectionRefs() const {
    std::vector<SketchRef> out;
    for (const auto& p : selection_) {
        if (p.loop < -1 || p.seg < 0) continue;
        switch (p.what) {
            case SketchPick::What::Vertex: out.push_back({p.loop, p.seg, SketchRef::Part::Start}); break;
            case SketchPick::What::Segment:
            case SketchPick::What::ArcMiddle:
            case SketchPick::What::Radius: out.push_back({p.loop, p.seg, SketchRef::Part::Edge}); break;
            case SketchPick::What::Centre: out.push_back({p.loop, p.seg, SketchRef::Part::Centre}); break;
            default: break;
        }
    }
    return out;
}

bool SketchCanvas::selected(const SketchPick& p) const {
    for (const auto& q : selection_)
        if (q == p) return true;
    return false;
}

void SketchCanvas::dropOnSegment(int loop, int seg) {
    const auto n = cons_.size();
    std::erase_if(cons_, [&](const SketchConstraint& c) {
        for (const auto& r : c.refs)
            if (r.loop == loop && r.segment == seg && (r.part == SketchRef::Part::Edge || r.part == SketchRef::Part::Centre)) return true;
        return false;
    });
    if (cons_.size() != n) {
        pickedCons_ = -1;
        emit constraintsChanged();
    }
}

void SketchCanvas::dropOnLoop(int loop, bool renumber) {
    const auto n = cons_.size();
    std::erase_if(cons_, [&](const SketchConstraint& c) {
        for (const auto& r : c.refs)
            if (r.loop == loop) return true;
        return false;
    });
    if (renumber)
        for (auto& c : cons_)
            for (auto& r : c.refs)
                if (r.loop > loop) --r.loop;
    if (cons_.size() != n || renumber) {
        pickedCons_ = -1;
        emit constraintsChanged();
    }
}

void SketchCanvas::afterSplit(int loop, int seg) {
    // Segment `seg` became two: what was on it as a whole no longer fits; its
    // end is now the second half's end; later segments move up one.
    dropOnSegment(loop, seg);
    for (auto& c : cons_)
        for (auto& r : c.refs) {
            if (r.loop != loop) continue;
            if (r.segment > seg) ++r.segment;
            else if (r.segment == seg && r.part == SketchRef::Part::End) ++r.segment;
        }
    emit constraintsChanged();
}

void SketchCanvas::addAutoConstraints(int loop) {
    // Lines drawn within a degree of horizontal or vertical stay so.
    if (!autoCons_ || loop < 0 || loop >= static_cast<int>(prof_.loops.size())) return;
    const auto& sg = prof_.loops[static_cast<std::size_t>(loop)].segments;
    const double tol = std::tan(kPi / 180.0);
    bool any = false;
    for (int s = 0; s < static_cast<int>(sg.size()); ++s) {
        const Segment& x = sg[static_cast<std::size_t>(s)];
        if (x.kind != Segment::Kind::Line) continue;
        const Vec2d d = x.b - x.a;
        SketchConstraint c;
        c.refs = {{loop, s, SketchRef::Part::Edge}};
        if (std::abs(d.y()) <= tol * std::abs(d.x())) c.kind = ConstraintKind::Horizontal;
        else if (std::abs(d.x()) <= tol * std::abs(d.y())) c.kind = ConstraintKind::Vertical;
        else continue;
        cons_.push_back(c);
        any = true;
    }
    if (any) emit constraintsChanged();
}

Vec2d SketchCanvas::anchorOf(const SketchRef& r) const {
    if (r.loop == -1) return r.part == SketchRef::Part::Edge ? (r.segment == 1 ? Vec2d(0, 30.0 / scale_) : Vec2d(30.0 / scale_, 0)) : Vec2d(0, 0);
    const Segment& s = prof_.loops[static_cast<std::size_t>(r.loop)].segments[static_cast<std::size_t>(r.segment)];
    switch (r.part) {
        case SketchRef::Part::Start: return s.kind == Segment::Kind::Circle ? s.centre : s.a;
        case SketchRef::Part::End: return s.kind == Segment::Kind::Circle ? s.centre : s.b;
        case SketchRef::Part::Centre: return s.centre;
        case SketchRef::Part::Edge: break;
    }
    switch (s.kind) {
        case Segment::Kind::Line: return 0.5 * (s.a + s.b);
        case Segment::Kind::Arc: return arcMiddle(s);
        case Segment::Kind::Circle: return s.centre + s.radius * Vec2d(std::sqrt(0.5), std::sqrt(0.5));
        case Segment::Kind::Spline: {
            const auto pts = s.sample(std::max(1e-3, s.length() / 20));
            return pts[pts.size() / 2];
        }
    }
    return s.a;
}

QString SketchCanvas::kindTitle(ConstraintKind k) const {
    switch (k) {
        case ConstraintKind::Coincident: return tr("Coincident");
        case ConstraintKind::Horizontal: return tr("Horizontal");
        case ConstraintKind::Vertical: return tr("Vertical");
        case ConstraintKind::Parallel: return tr("Parallel");
        case ConstraintKind::Perpendicular: return tr("Perpendicular");
        case ConstraintKind::Tangent: return tr("Tangent");
        case ConstraintKind::Equal: return tr("Equal");
        case ConstraintKind::Distance: return tr("Distance");
        case ConstraintKind::DistanceX: return tr("Horizontal distance");
        case ConstraintKind::DistanceY: return tr("Vertical distance");
        case ConstraintKind::Radius: return tr("Radius");
        case ConstraintKind::Diameter: return tr("Diameter");
        case ConstraintKind::Angle: return tr("Angle");
        case ConstraintKind::Fix: return tr("Fixed");
        case ConstraintKind::OnObject: return tr("Point on object");
        case ConstraintKind::Symmetric: return tr("Symmetric");
        case ConstraintKind::Midpoint: return tr("Midpoint");
    }
    return {};
}

QString SketchCanvas::labelOf(const SketchConstraint& c) const {
    QLocale loc;
    double v = c.value;
    if (!c.parameter.empty())
        if (const auto it = params_.find(c.parameter); it != params_.end()) v = it->second;
    QString num = loc.toString(v, 'f', 3);
    while (num.contains(loc.decimalPoint()) && (num.endsWith('0') || num.endsWith(loc.decimalPoint()))) num.chop(1);
    if (!c.parameter.empty()) num = QString::fromStdString(c.parameter) + " = " + num;
    switch (c.kind) {
        case ConstraintKind::Coincident: return QStringLiteral("◉");
        case ConstraintKind::Horizontal: return QStringLiteral("H");
        case ConstraintKind::Vertical: return QStringLiteral("V");
        case ConstraintKind::Parallel: return QStringLiteral("∥");
        case ConstraintKind::Perpendicular: return QStringLiteral("⊥");
        case ConstraintKind::Tangent: return QStringLiteral("T");
        case ConstraintKind::Equal: return QStringLiteral("=");
        case ConstraintKind::Fix: return QStringLiteral("⚓");
        case ConstraintKind::OnObject: return QStringLiteral("∈");
        case ConstraintKind::Symmetric: return QStringLiteral("⇋");
        case ConstraintKind::Midpoint: return QStringLiteral("M");
        case ConstraintKind::Distance: return num;
        case ConstraintKind::DistanceX: return QStringLiteral("↔ ") + num;
        case ConstraintKind::DistanceY: return QStringLiteral("↕ ") + num;
        case ConstraintKind::Radius: return QStringLiteral("R ") + num;
        case ConstraintKind::Diameter: return QStringLiteral("Ø ") + num;
        case ConstraintKind::Angle: return num + QStringLiteral("°");
    }
    return {};
}

int SketchCanvas::constraintAt(const QPointF& at) const {
    for (auto it = tagRects_.rbegin(); it != tagRects_.rend(); ++it)
        if (it->first.contains(at)) return it->second;
    return -1;
}

// ============================================================== dialog ==

SketchEditor::SketchEditor(const Profile& profile, const QString& name, const std::vector<Body>& bodies, const QString& lang, QWidget* parent,
                           const std::vector<SketchConstraint>& constraints, const std::map<std::string, double>& parameters)
    : QDialog(parent), bodies_(bodies) {
    (void)lang;
    setWindowTitle(tr("Sketch: %1").arg(name));
    resize(1100, 720);
    canvas_ = new SketchCanvas(this);
    canvas_->setProfile(profile);
    canvas_->setConstraints(constraints, parameters);

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
    tb->addAction(tr("Construction"), this, [this]() { canvas_->toggleConstruction(); })
        ->setToolTip(tr("Make the picked loop construction geometry (dashed: it makes no material, it is there to hang dimensions on), or make it material again"));
    tb->addSeparator();
    tb->addAction(tr("Undo"), this, [this]() { canvas_->undo(); })->setShortcut(QKeySequence::Undo);
    tb->addAction(tr("Redo"), this, [this]() { canvas_->redo(); })->setShortcut(QKeySequence::Redo);
    tb->addAction(tr("Fit"), this, [this]() { canvas_->fit(); })->setToolTip(tr("Fit the sketch in the window (F)"));

    // Constraints and dimensions, on what is picked (Shift or Ctrl+click picks several).
    auto* tc = new QToolBar(this);
    tc->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto con = [&](const QString& text, ConstraintKind k) {
        tc->addAction(text, this, [this, k]() { constrain(k); })->setToolTip(hint(k));
    };
    con(tr("Coincident"), ConstraintKind::Coincident);
    con(tr("Horizontal"), ConstraintKind::Horizontal);
    con(tr("Vertical"), ConstraintKind::Vertical);
    con(tr("Parallel"), ConstraintKind::Parallel);
    con(tr("Perpendicular"), ConstraintKind::Perpendicular);
    con(tr("Tangent"), ConstraintKind::Tangent);
    con(tr("Equal"), ConstraintKind::Equal);
    con(tr("Fix"), ConstraintKind::Fix);
    con(tr("On object"), ConstraintKind::OnObject);
    con(tr("Midpoint"), ConstraintKind::Midpoint);
    con(tr("Symmetric"), ConstraintKind::Symmetric);
    tc->addSeparator();
    con(tr("Distance"), ConstraintKind::Distance);
    con(tr("Horizontal distance"), ConstraintKind::DistanceX);
    con(tr("Vertical distance"), ConstraintKind::DistanceY);
    con(tr("Radius"), ConstraintKind::Radius);
    con(tr("Diameter"), ConstraintKind::Diameter);
    con(tr("Angle"), ConstraintKind::Angle);
    tc->addSeparator();
    tc->addAction(tr("+ Origin"), this, [this]() { canvas_->selectReference(0); })->setToolTip(tr("Add the sketch's origin to the selection, to constrain or dimension against it"));
    tc->addAction(tr("+ X axis"), this, [this]() { canvas_->selectReference(1); })->setToolTip(tr("Add the sketch's x axis to the selection"));
    tc->addAction(tr("+ Y axis"), this, [this]() { canvas_->selectReference(2); })->setToolTip(tr("Add the sketch's y axis to the selection"));

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
    sl->addSpacing(8);
    sl->addWidget(new QLabel(tr("<b>Constraints</b>")));
    autoCons_ = new QCheckBox(tr("Horizontal and vertical lines stay so when drawn"));
    autoCons_->setChecked(true);
    sl->addWidget(autoCons_);
    dofLabel_ = new QLabel();
    dofLabel_->setWordWrap(true);
    sl->addWidget(dofLabel_);
    consList_ = new QListWidget();
    consList_->setMinimumHeight(120);
    consList_->setToolTip(tr("Double-click a dimension to change its value: a number, or the name of a parameter of the part"));
    sl->addWidget(consList_, 1);
    auto* cb = new QHBoxLayout();
    auto* editV = new QPushButton(tr("Change value…"));
    auto* removeC = new QPushButton(tr("Remove"));
    cb->addWidget(editV);
    cb->addWidget(removeC);
    sl->addLayout(cb);
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
    vl->addWidget(tc);
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
    connect(autoCons_, &QCheckBox::toggled, this, [this](bool on) { canvas_->setAutoConstraints(on); });
    connect(canvas_, &SketchCanvas::constraintsChanged, this, [this]() { updateConstraintList(); updateStatus(); });
    connect(canvas_, &SketchCanvas::constraintPicked, this, [this](int i) {
        const QSignalBlocker block(consList_);
        consList_->setCurrentRow(i);
    });
    connect(canvas_, &SketchCanvas::constraintEditRequested, this, [this](int i) { editConstraintValue(i); });
    connect(consList_, &QListWidget::currentRowChanged, this, [this](int i) { canvas_->setPickedConstraint(i); });
    connect(consList_, &QListWidget::itemDoubleClicked, this, [this]() { editConstraintValue(consList_->currentRow()); });
    connect(editV, &QPushButton::clicked, this, [this]() { editConstraintValue(consList_->currentRow()); });
    connect(removeC, &QPushButton::clicked, this, [this]() { canvas_->removeConstraint(consList_->currentRow()); });
    canvas_->setTolerance(tol_->value());
    updateConstraintList();
    loadReference();
    updatePanel();
    updateStatus();
    QMetaObject::invokeMethod(canvas_, [this]() { canvas_->fit(); }, Qt::QueuedConnection);
}

Profile SketchEditor::profile() const { return canvas_->profile(); }

std::vector<SketchConstraint> SketchEditor::constraints() const { return canvas_->constraints(); }

QString SketchEditor::hint(ConstraintKind k) const {
    switch (k) {
        case ConstraintKind::Coincident: return tr("Two points in the same place");
        case ConstraintKind::Horizontal: return tr("A line, or two points, horizontal");
        case ConstraintKind::Vertical: return tr("A line, or two points, vertical");
        case ConstraintKind::Parallel: return tr("Two lines parallel");
        case ConstraintKind::Perpendicular: return tr("Two lines at right angles");
        case ConstraintKind::Tangent: return tr("A line and an arc or circle, or two arcs or circles, touching smoothly");
        case ConstraintKind::Equal: return tr("Two lines of the same length, or two arcs or circles of the same radius");
        case ConstraintKind::Fix: return tr("A point (or a circle's centre) stays where it is");
        case ConstraintKind::OnObject: return tr("A point on a line, arc or circle");
        case ConstraintKind::Midpoint: return tr("A point at the middle of a line");
        case ConstraintKind::Symmetric: return tr("Two points symmetric about a line or a point");
        case ConstraintKind::Distance: return tr("A line's length, or the distance between two points or from a point to a line");
        case ConstraintKind::DistanceX: return tr("Along x: a line, or two points");
        case ConstraintKind::DistanceY: return tr("Along y: a line, or two points");
        case ConstraintKind::Radius: return tr("An arc's or a circle's radius");
        case ConstraintKind::Diameter: return tr("An arc's or a circle's diameter");
        case ConstraintKind::Angle: return tr("The angle between two lines, in degrees");
    }
    return {};
}

void SketchEditor::updateConstraintList() {
    const QSignalBlocker block(consList_);
    consList_->clear();
    const auto& cs = canvas_->constraints();
    for (std::size_t i = 0; i < cs.size(); ++i) {
        QString text = QString("%1. %2").arg(i + 1).arg(canvas_->kindTitle(cs[i].kind));
        if (constraintHasValue(cs[i].kind)) text += ": " + canvas_->labelOf(cs[i]);
        consList_->addItem(text);
    }
    consList_->setCurrentRow(canvas_->pickedConstraint());
}

bool SketchEditor::readValue(const QString& typed, double* value, std::string* parameter, QString* why) const {
    const QString t = typed.trimmed();
    bool ok = false;
    double v = QLocale().toDouble(t, &ok);
    if (!ok) v = QLocale::c().toDouble(t, &ok);
    if (ok) {
        *value = v;
        parameter->clear();
        return true;
    }
    const auto& params = canvas_->parameters();
    if (const auto it = params.find(t.toStdString()); it != params.end()) {
        *value = it->second;
        *parameter = it->first;
        return true;
    }
    QStringList names;
    for (const auto& [n, _] : params) names << QString::fromStdString(n);
    *why = names.isEmpty() ? tr("\"%1\" is not a number, and the part has no parameters.").arg(t)
                           : tr("\"%1\" is not a number nor a parameter of the part (%2).").arg(t, names.join(", "));
    return false;
}

void SketchEditor::constrain(ConstraintKind kind) {
    std::vector<SketchRef> refs = canvas_->selectionRefs();
    const Profile& prof = canvas_->profile();
    static const Segment axes[2] = {Segment::line({0, 0}, {1, 0}), Segment::line({0, 0}, {0, 1})};
    auto seg = [&](const SketchRef& r) -> const Segment& {
        if (r.loop == -1) return axes[r.segment == 1 ? 1 : 0];
        return prof.loops[static_cast<std::size_t>(r.loop)].segments[static_cast<std::size_t>(r.segment)];
    };
    auto isPoint = [](const SketchRef& r) { return r.part != SketchRef::Part::Edge; };
    // Points first where the solver wants them so (a point on a line, the middle of a line, symmetric about).
    if (kind == ConstraintKind::OnObject || kind == ConstraintKind::Midpoint || kind == ConstraintKind::Symmetric)
        std::stable_partition(refs.begin(), refs.end(), isPoint);
    // A circle picked for a point-only constraint means its centre.
    if (kind == ConstraintKind::Fix || kind == ConstraintKind::Coincident)
        for (auto& r : refs)
            if (r.part == SketchRef::Part::Edge && seg(r).kind == Segment::Kind::Circle) r.part = SketchRef::Part::Centre;
    // Horizontal or vertical distance of one line: between its two ends.
    if ((kind == ConstraintKind::DistanceX || kind == ConstraintKind::DistanceY) && refs.size() == 1 && refs[0].part == SketchRef::Part::Edge &&
        seg(refs[0]).kind == Segment::Kind::Line)
        refs = {{refs[0].loop, refs[0].segment, SketchRef::Part::Start}, {refs[0].loop, refs[0].segment, SketchRef::Part::End}};
    std::size_t need = 2;
    switch (kind) {
        case ConstraintKind::Horizontal:
        case ConstraintKind::Vertical:
        case ConstraintKind::Distance:
            need = !refs.empty() && refs[0].part == SketchRef::Part::Edge && refs.size() == 1 ? 1 : 2;
            break;
        case ConstraintKind::Fix:
        case ConstraintKind::Radius:
        case ConstraintKind::Diameter: need = 1; break;
        case ConstraintKind::Symmetric: need = 3; break;
        default: need = 2; break;
    }
    if (refs.size() < need) {
        QMessageBox::information(this, tr("Constraint"),
                                 tr("Pick first what it applies to (Shift or Ctrl+click picks several): %1.").arg(hint(kind).toLower()));
        return;
    }
    refs.resize(need);
    SketchConstraint c;
    c.kind = kind;
    c.refs = refs;
    if (constraintHasValue(kind)) {
        auto now = canvas_->measure(c);
        if (now && *now < 0 && (kind == ConstraintKind::DistanceX || kind == ConstraintKind::DistanceY)) {
            std::swap(c.refs[0], c.refs[1]);
            now = canvas_->measure(c);
        }
        if (!now) {
            QMessageBox::information(this, tr("Constraint"), tr("This dimension does not apply to the picked pieces."));
            return;
        }
        bool ok = false;
        const QString typed = QInputDialog::getText(this, canvas_->kindTitle(kind), tr("Value (a number, or the name of a parameter of the part):"), QLineEdit::Normal,
                                                    QLocale().toString(*now, 'f', 3), &ok);
        if (!ok) return;
        QString why;
        if (!readValue(typed, &c.value, &c.parameter, &why)) {
            QMessageBox::information(this, tr("Constraint"), why);
            return;
        }
    }
    if (const QString why = canvas_->addConstraint(c); !why.isEmpty())
        QMessageBox::information(this, tr("Constraint"), tr("It cannot be added: %1").arg(why));
}

void SketchEditor::editConstraintValue(int index) {
    const auto& cs = canvas_->constraints();
    if (index < 0 || index >= static_cast<int>(cs.size()) || !constraintHasValue(cs[static_cast<std::size_t>(index)].kind)) return;
    const SketchConstraint c = cs[static_cast<std::size_t>(index)];
    bool ok = false;
    const QString now = c.parameter.empty() ? QLocale().toString(c.value, 'f', 3) : QString::fromStdString(c.parameter);
    const QString typed = QInputDialog::getText(this, canvas_->kindTitle(c.kind), tr("Value (a number, or the name of a parameter of the part):"), QLineEdit::Normal, now, &ok);
    if (!ok) return;
    double v = 0;
    std::string param;
    QString why;
    if (!readValue(typed, &v, &param, &why)) {
        QMessageBox::information(this, tr("Constraint"), why);
        return;
    }
    if (const QString w = canvas_->setConstraintValue(index, v, param); !w.isEmpty())
        QMessageBox::information(this, tr("Constraint"), tr("That value cannot hold: %1").arg(w));
}

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
    if (canvas_->constraints().empty()) dofLabel_->setText(tr("No constraints: every point is free."));
    else if (!canvas_->solved()) dofLabel_->setText("<span style='color:#dc2828'>" + tr("The constraints do not hold: %1").arg(canvas_->solveMessage()) + "</span>");
    else if (canvas_->dof() == 0) dofLabel_->setText("<span style='color:#28a046'>" + tr("Fully constrained.") + "</span>");
    else dofLabel_->setText(tr("Degrees of freedom left: %1").arg(canvas_->dof()));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(!empty && why.empty() && canvas_->solved());
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

// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Icons.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>
#include <functional>

namespace orv {
namespace {

const QColor kLine(58, 104, 150);    // the drawing
const QColor kAccent(224, 132, 48);  // what the tool adds or changes
const QColor kSoft(150, 178, 204);   // context

using Glyph = std::function<void(QPainter&)>;

QPen pen(const QColor& c, double w = 4.0, Qt::PenStyle st = Qt::SolidLine) {
    QPen p(c, w, st, Qt::RoundCap, Qt::RoundJoin);
    return p;
}

void box(QPainter& p, double x, double y, double s, const QColor& c, double h = -1) {
    if (h < 0) h = s * 0.6;
    const double d = s * 0.35;
    QPainterPath path;
    path.moveTo(x, y + d);
    path.lineTo(x + s * 0.5, y);
    path.lineTo(x + s, y + d);
    path.lineTo(x + s, y + d + h);
    path.lineTo(x + s * 0.5, y + 2 * d + h);
    path.lineTo(x, y + d + h);
    path.closeSubpath();
    path.moveTo(x, y + d);
    path.lineTo(x + s * 0.5, y + 2 * d);
    path.lineTo(x + s, y + d);
    path.moveTo(x + s * 0.5, y + 2 * d);
    path.lineTo(x + s * 0.5, y + 2 * d + h);
    p.setPen(pen(c));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void arrowUp(QPainter& p, double x, double y0, double y1, const QColor& c) {
    p.setPen(pen(c, 5));
    p.drawLine(QPointF(x, y0), QPointF(x, y1 + 6));
    QPainterPath head;
    head.moveTo(x - 9, y1 + 12);
    head.lineTo(x, y1);
    head.lineTo(x + 9, y1 + 12);
    p.drawPath(head);
}

void wave(QPainter& p, double y, const QColor& c, double amp = 8, double w = 4) {
    QPainterPath s;
    s.moveTo(8, y);
    s.cubicTo(22, y - amp * 2, 34, y + amp * 2, 56, y);
    p.setPen(pen(c, w));
    p.setBrush(Qt::NoBrush);
    p.drawPath(s);
}

void dots(QPainter& p, const QColor& c, bool scatter) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const double pts[][2] = {{16, 20}, {30, 14}, {46, 22}, {20, 36}, {36, 32}, {50, 42}, {14, 50}, {30, 48}, {44, 54}};
    for (auto& q : pts) p.drawEllipse(QPointF(q[0] + (scatter ? 0 : 0), q[1]), 3.5, 3.5);
}

void tri(QPainter& p, int n, const QColor& c) {
    p.setPen(pen(c, 2.5));
    p.setBrush(Qt::NoBrush);
    const QPointF a(32, 8), b(8, 56), d(56, 56);
    p.drawPolygon(QPolygonF({a, b, d}));
    for (int i = 1; i < n; ++i) {
        const double t = double(i) / n;
        p.drawLine(a + (b - a) * t, a + (d - a) * t);
        p.drawLine(b + (a - b) * t, b + (d - b) * t);
        p.drawLine(d + (a - d) * t, d + (b - d) * t);
    }
}

void magnifier(QPainter& p, const QColor& c) {
    p.setPen(pen(c, 5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(27, 27), 15, 15);
    p.drawLine(QPointF(38, 38), QPointF(54, 54));
}

void cross(QPainter& p, double x, double y, double r, const QColor& c) {
    p.setPen(pen(c, 5));
    p.drawLine(QPointF(x - r, y - r), QPointF(x + r, y + r));
    p.drawLine(QPointF(x + r, y - r), QPointF(x - r, y + r));
}

void pencil(QPainter& p, const QColor& c) {
    p.setPen(pen(c, 4));
    p.setBrush(Qt::NoBrush);
    QPainterPath s;
    s.moveTo(40, 10);
    s.lineTo(54, 24);
    s.lineTo(26, 52);
    s.lineTo(10, 54);
    s.lineTo(12, 38);
    s.closeSubpath();
    p.drawPath(s);
    p.drawLine(QPointF(34, 16), QPointF(48, 30));
}

QHash<QString, Glyph> glyphs() {
    QHash<QString, Glyph> g;
    g["import"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawPolyline(QPolygonF({QPointF(8, 38), QPointF(8, 54), QPointF(56, 54), QPointF(56, 38)}));
        p.setPen(pen(kAccent, 5));
        p.drawLine(QPointF(32, 8), QPointF(32, 40));
        p.drawPolyline(QPolygonF({QPointF(22, 30), QPointF(32, 42), QPointF(42, 30)}));
    };
    g["palette"] = [](QPainter& p) { magnifier(p, kLine); };
    g["guide"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawRect(QRectF(12, 10, 40, 46));
        p.setPen(pen(kAccent, 3));
        for (int y : {22, 32, 42}) p.drawLine(QPointF(20, y), QPointF(44, y));
    };
    g["part.new"] = [](QPainter& p) {
        box(p, 10, 12, 44, kLine);
        p.setPen(pen(kAccent, 5));
        p.drawLine(QPointF(50, 6), QPointF(50, 22));
        p.drawLine(QPointF(42, 14), QPointF(58, 14));
    };
    g["part.sketch"] = [](QPainter& p) {
        p.setPen(pen(kSoft, 3, Qt::DashLine));
        p.drawRect(QRectF(8, 20, 34, 34));
        pencil(p, kAccent);
    };
    g["edit_sketch"] = g["part.sketch"];
    g["part.edit_sketch"] = g["part.sketch"];
    g["sketch_on_face"] = [](QPainter& p) {
        box(p, 8, 16, 40, kLine);
        p.setPen(pen(kAccent, 4));
        p.drawEllipse(QPointF(28, 24), 10, 5);
        p.drawLine(QPointF(44, 6), QPointF(56, 18));
    };
    g["part.sketch_from_section"] = [](QPainter& p) {
        p.setPen(pen(kSoft, 3));
        p.drawEllipse(QPointF(32, 34), 22, 16);
        p.setPen(pen(kAccent, 4));
        p.drawLine(QPointF(6, 34), QPointF(58, 34));
        p.drawEllipse(QPointF(32, 34), 4, 4);
    };
    g["part.extrude"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawPolygon(QPolygonF({QPointF(8, 46), QPointF(30, 56), QPointF(52, 46), QPointF(30, 36)}));
        p.setPen(pen(kAccent, 3, Qt::DashLine));
        p.drawLine(QPointF(8, 46), QPointF(8, 26));
        p.drawLine(QPointF(52, 46), QPointF(52, 26));
        arrowUp(p, 30, 42, 6, kAccent);
    };
    g["part.revolve"] = [](QPainter& p) {
        p.setPen(pen(kSoft, 3, Qt::DashLine));
        p.drawLine(QPointF(32, 4), QPointF(32, 60));
        p.setPen(pen(kAccent, 4));
        p.drawEllipse(QPointF(32, 40), 24, 10);
        p.setPen(pen(kLine, 4));
        p.drawLine(QPointF(10, 16), QPointF(10, 40));
        p.drawLine(QPointF(54, 16), QPointF(54, 40));
        p.drawEllipse(QPointF(32, 16), 22, 8);
    };
    g["part.loft"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawEllipse(QPointF(32, 50), 22, 8);
        p.drawRect(QRectF(20, 8, 24, 12));
        p.setPen(pen(kAccent, 3));
        p.drawLine(QPointF(10, 50), QPointF(20, 20));
        p.drawLine(QPointF(54, 50), QPointF(44, 20));
    };
    g["part.pattern"] = [](QPainter& p) {
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                p.setBrush(i == 0 && j == 0 ? kLine : kAccent);
                p.drawRoundedRect(QRectF(8 + i * 18, 8 + j * 18, 12, 12), 3, 3);
            }
    };
    g["part.fillet"] = [](QPainter& p) {
        p.setPen(pen(kSoft, 3, Qt::DashLine));
        p.drawPolyline(QPolygonF({QPointF(10, 54), QPointF(10, 10), QPointF(54, 10)}));
        p.setPen(pen(kAccent, 5));
        QPainterPath s;
        s.moveTo(10, 54);
        s.lineTo(10, 34);
        s.quadTo(10, 10, 34, 10);
        s.lineTo(54, 10);
        p.drawPath(s);
    };
    g["fillet_edges"] = g["part.fillet"];
    g["part.chamfer"] = [](QPainter& p) {
        p.setPen(pen(kSoft, 3, Qt::DashLine));
        p.drawPolyline(QPolygonF({QPointF(10, 54), QPointF(10, 10), QPointF(54, 10)}));
        p.setPen(pen(kAccent, 5));
        p.drawPolyline(QPolygonF({QPointF(10, 54), QPointF(10, 30), QPointF(30, 10), QPointF(54, 10)}));
    };
    g["chamfer_edges"] = g["part.chamfer"];
    g["part.hole"] = [](QPainter& p) {
        p.setPen(pen(kLine, 3));
        p.drawRoundedRect(QRectF(8, 14, 48, 36), 4, 4);
        p.setPen(pen(kAccent, 4));
        p.drawEllipse(QPointF(32, 32), 11, 11);
        p.drawLine(QPointF(32, 25), QPointF(32, 39));
    };
    g["part.mirror"] = [](QPainter& p) {
        p.setPen(pen(kSoft, 3, Qt::DashLine));
        p.drawLine(QPointF(32, 6), QPointF(32, 58));
        p.setPen(pen(kAccent, 4));
        p.drawPolyline(QPolygonF({QPointF(28, 14), QPointF(10, 14), QPointF(10, 50), QPointF(28, 50)}));
        p.setPen(pen(kLine, 4));
        p.drawPolyline(QPolygonF({QPointF(36, 14), QPointF(54, 14), QPointF(54, 50), QPointF(36, 50)}));
    };
    g["part.move"] = [](QPainter& p) {
        p.setPen(pen(kAccent, 4));
        p.drawLine(QPointF(32, 8), QPointF(32, 56));
        p.drawLine(QPointF(8, 32), QPointF(56, 32));
        for (auto [x, y, a] : {std::tuple{32., 8., 0.}, {32., 56., 180.}, {8., 32., 270.}, {56., 32., 90.}}) {
            p.save();
            p.translate(x, y);
            p.rotate(a);
            p.drawPolyline(QPolygonF({QPointF(-7, 8), QPointF(0, 0), QPointF(7, 8)}));
            p.restore();
        }
    };
    g["part.edit"] = [](QPainter& p) {
        p.setPen(pen(kLine, 4));
        for (int y : {16, 32, 48}) p.drawLine(QPointF(8, y), QPointF(56, y));
        p.setPen(Qt::NoPen);
        p.setBrush(kAccent);
        p.drawEllipse(QPointF(22, 16), 6, 6);
        p.drawEllipse(QPointF(42, 32), 6, 6);
        p.drawEllipse(QPointF(28, 48), 6, 6);
    };
    g["part.parameter"] = [](QPainter& p) {
        p.setPen(pen(kLine, 4));
        p.drawLine(QPointF(8, 40), QPointF(56, 40));
        p.drawLine(QPointF(8, 32), QPointF(8, 48));
        p.drawLine(QPointF(56, 32), QPointF(56, 48));
        p.setPen(pen(kAccent, 4));
        p.drawRoundedRect(QRectF(16, 10, 32, 18), 4, 4);
    };
    g["part.constrain"] = [](QPainter& p) {
        // A right angle with its square mark and a padlock-like dimension.
        p.setPen(pen(kLine, 4));
        p.drawLine(QPointF(12, 10), QPointF(12, 52));
        p.drawLine(QPointF(12, 52), QPointF(54, 52));
        p.setPen(pen(kAccent, 4));
        p.drawRect(QRectF(12, 38, 14, 14));
        p.drawLine(QPointF(34, 14), QPointF(54, 14));
        p.drawLine(QPointF(34, 8), QPointF(34, 20));
        p.drawLine(QPointF(54, 8), QPointF(54, 20));
    };
    g["part.delete"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawRect(QRectF(16, 18, 32, 38));
        p.drawLine(QPointF(10, 18), QPointF(54, 18));
        p.drawLine(QPointF(26, 10), QPointF(38, 10));
        p.setPen(pen(kAccent, 3));
        for (int x : {26, 32, 38}) p.drawLine(QPointF(x, 26), QPointF(x, 48));
    };
    g["delete_selection"] = g["part.delete"];
    g["mesh.delete_faces"] = g["part.delete"];
    g["part.surface_from_scan"] = [](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(kSoft);
        for (int i = 0; i < 6; ++i) p.drawEllipse(QPointF(10 + i * 9, 44 - 6 * std::sin(i)), 3, 3);
        wave(p, 32, kAccent, 7, 5);
    };
    g["part.cut_by_surface"] = [](QPainter& p) {
        box(p, 10, 14, 44, kLine);
        wave(p, 34, kAccent, 6, 5);
    };
    g["part.thicken"] = [](QPainter& p) {
        wave(p, 24, kLine, 6, 4);
        wave(p, 42, kAccent, 6, 4);
    };
    g["part.compare"] = [](QPainter& p) {
        QLinearGradient gr(8, 0, 56, 0);
        gr.setColorAt(0, QColor(60, 110, 200));
        gr.setColorAt(0.5, QColor(80, 180, 90));
        gr.setColorAt(1, QColor(220, 70, 50));
        p.setPen(Qt::NoPen);
        p.setBrush(gr);
        p.drawRoundedRect(QRectF(8, 22, 48, 20), 5, 5);
    };
    g["body.clear_deviation"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawRoundedRect(QRectF(8, 22, 48, 20), 5, 5);
        cross(p, 48, 14, 6, kAccent);
    };
    g["mesh.section"] = [](QPainter& p) {
        box(p, 12, 10, 40, kSoft);
        p.setPen(pen(kAccent, 4));
        p.setBrush(QColor(224, 132, 48, 60));
        p.drawPolygon(QPolygonF({QPointF(4, 36), QPointF(32, 24), QPointF(60, 36), QPointF(32, 48)}));
    };
    g["mesh.analyze"] = [](QPainter& p) {
        tri(p, 2, kSoft);
        magnifier(p, kAccent);
    };
    g["mesh.repair"] = [](QPainter& p) {
        p.setPen(pen(kLine, 6));
        p.drawLine(QPointF(14, 50), QPointF(36, 28));
        p.setPen(pen(kAccent, 5));
        p.drawArc(QRectF(30, 8, 26, 26), 200 * 16, 270 * 16);
    };
    g["mesh.fill_holes"] = [](QPainter& p) {
        tri(p, 1, kSoft);
        p.setPen(pen(kAccent, 3));
        p.setBrush(QColor(224, 132, 48, 110));
        p.drawEllipse(QPointF(32, 40), 11, 9);
    };
    g["mesh.remove_pieces"] = [](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(kLine);
        p.drawEllipse(QPointF(24, 34), 16, 16);
        p.setBrush(kSoft);
        p.drawEllipse(QPointF(50, 14), 5, 5);
        p.drawEllipse(QPointF(52, 50), 4, 4);
        cross(p, 51, 32, 5, kAccent);
    };
    g["mesh.flip_normals"] = [](QPainter& p) {
        p.setPen(pen(kLine, 4));
        p.drawLine(QPointF(8, 32), QPointF(56, 32));
        arrowUp(p, 24, 30, 6, kAccent);
        p.save();
        p.translate(0, 64);
        p.scale(1, -1);
        arrowUp(p, 42, 30, 6, kSoft);
        p.restore();
    };
    g["mesh.smooth"] = [](QPainter& p) {
        p.setPen(pen(kSoft, 3));
        p.drawPolyline(QPolygonF({QPointF(8, 24), QPointF(16, 14), QPointF(24, 28), QPointF(32, 12), QPointF(40, 26), QPointF(48, 14), QPointF(56, 22)}));
        wave(p, 46, kAccent, 3, 5);
    };
    g["mesh.decimate"] = [](QPainter& p) { tri(p, 2, kAccent); };
    g["mesh.remesh"] = [](QPainter& p) { tri(p, 4, kAccent); };
    g["mesh.remesh_quads"] = [](QPainter& p) {
        p.setPen(pen(kAccent, 3));
        for (int i = 0; i <= 3; ++i) {
            p.drawLine(QPointF(8 + i * 16, 8), QPointF(8 + i * 16, 56));
            p.drawLine(QPointF(8, 8 + i * 16), QPointF(56, 8 + i * 16));
        }
    };
    g["mesh.remove_plane"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawPolygon(QPolygonF({QPointF(4, 44), QPointF(32, 32), QPointF(60, 44), QPointF(32, 56)}));
        p.setPen(Qt::NoPen);
        p.setBrush(kSoft);
        p.drawEllipse(QPointF(32, 22), 12, 12);
        cross(p, 48, 12, 6, kAccent);
    };
    g["mesh.remove_outliers"] = [](QPainter& p) {
        dots(p, kLine, true);
        p.setPen(pen(kAccent, 3));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(14, 50), 8, 8);
    };
    g["mesh.crop"] = [](QPainter& p) {
        p.setPen(pen(kAccent, 5));
        p.drawPolyline(QPolygonF({QPointF(16, 4), QPointF(16, 48), QPointF(60, 48)}));
        p.drawPolyline(QPolygonF({QPointF(4, 16), QPointF(48, 16), QPointF(48, 60)}));
    };
    g["mesh.merge"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawEllipse(QPointF(24, 32), 16, 16);
        p.setPen(pen(kAccent));
        p.drawEllipse(QPointF(40, 32), 16, 16);
    };
    g["lasso"] = [](QPainter& p) {
        p.setPen(pen(kAccent, 4, Qt::DashLine));
        QPainterPath s;
        s.moveTo(20, 50);
        s.cubicTo(-2, 30, 14, 6, 34, 8);
        s.cubicTo(58, 10, 60, 40, 36, 44);
        s.lineTo(20, 50);
        p.drawPath(s);
        p.setPen(pen(kLine, 4));
        p.drawLine(QPointF(20, 50), QPointF(14, 60));
    };
    g["points.thin"] = [](QPainter& p) { dots(p, kAccent, false); };
    g["points.estimate_normals"] = [](QPainter& p) {
        dots(p, kSoft, false);
        p.setPen(pen(kAccent, 3));
        p.drawLine(QPointF(30, 14), QPointF(30, 2));
        p.drawLine(QPointF(36, 32), QPointF(40, 20));
    };
    g["points.to_mesh"] = [](QPainter& p) {
        tri(p, 2, kAccent);
        dots(p, kLine, false);
    };
    g["align.principal"] = [](QPainter& p) {
        p.setPen(pen(kLine, 4));
        p.drawLine(QPointF(10, 54), QPointF(56, 54));
        p.drawLine(QPointF(10, 54), QPointF(10, 8));
        p.setPen(pen(kAccent, 4));
        p.drawLine(QPointF(10, 54), QPointF(40, 30));
    };
    g["align.datums"] = [](QPainter& p) {
        p.setPen(pen(kLine, 4));
        p.drawLine(QPointF(8, 54), QPointF(56, 54));
        p.drawLine(QPointF(8, 54), QPointF(8, 8));
        p.setPen(Qt::NoPen);
        p.setBrush(kAccent);
        p.drawRect(QRectF(14, 30, 20, 18));
    };
    g["align.best_fit"] = [](QPainter& p) {
        wave(p, 30, kLine, 8, 4);
        wave(p, 36, kAccent, 8, 4);
    };
    g["align.global"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawEllipse(QPointF(32, 32), 24, 24);
        p.setPen(pen(kAccent, 4));
        p.drawLine(QPointF(32, 4), QPointF(32, 60));
        p.drawLine(QPointF(4, 32), QPointF(60, 32));
    };
    g["align.point_pairs"] = [](QPainter& p) {
        p.setPen(pen(kSoft, 3, Qt::DashLine));
        p.drawLine(QPointF(14, 18), QPointF(48, 26));
        p.drawLine(QPointF(16, 46), QPointF(46, 52));
        p.setPen(Qt::NoPen);
        p.setBrush(kLine);
        p.drawEllipse(QPointF(14, 18), 6, 6);
        p.drawEllipse(QPointF(16, 46), 6, 6);
        p.setBrush(kAccent);
        p.drawEllipse(QPointF(48, 26), 6, 6);
        p.drawEllipse(QPointF(46, 52), 6, 6);
    };
    g["align.move"] = g["part.move"];
    g["features.segment"] = [](QPainter& p) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(80, 140, 210));
        p.drawRect(QRectF(8, 8, 22, 22));
        p.setBrush(kAccent);
        p.drawRect(QRectF(34, 8, 22, 22));
        p.setBrush(QColor(90, 175, 110));
        p.drawRect(QRectF(8, 34, 48, 22));
    };
    g["features.fit"] = [](QPainter& p) {
        p.setPen(pen(kAccent, 4));
        p.drawEllipse(QPointF(32, 14), 18, 7);
        p.drawLine(QPointF(14, 14), QPointF(14, 50));
        p.drawLine(QPointF(50, 14), QPointF(50, 50));
        p.drawArc(QRectF(14, 43, 36, 14), 180 * 16, 180 * 16);
    };
    g["features.refit"] = g["features.fit"];
    g["features.measure"] = [](QPainter& p) {
        p.setPen(pen(kLine, 4));
        p.drawRect(QRectF(6, 22, 52, 20));
        p.setPen(pen(kAccent, 3));
        for (int i = 0; i < 6; ++i) p.drawLine(QPointF(12 + i * 8, 22), QPointF(12 + i * 8, i % 2 ? 30 : 34));
    };
    g["fit_all"] = [](QPainter& p) {
        p.setPen(pen(kAccent, 5));
        p.drawPolyline(QPolygonF({QPointF(6, 20), QPointF(6, 6), QPointF(20, 6)}));
        p.drawPolyline(QPolygonF({QPointF(44, 6), QPointF(58, 6), QPointF(58, 20)}));
        p.drawPolyline(QPolygonF({QPointF(58, 44), QPointF(58, 58), QPointF(44, 58)}));
        p.drawPolyline(QPolygonF({QPointF(20, 58), QPointF(6, 58), QPointF(6, 44)}));
        box(p, 18, 16, 28, kLine);
    };
    g["agent_chat"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(6, 8, 52, 36), 10, 10);
        QPainterPath tail;
        tail.moveTo(18, 44);
        tail.lineTo(14, 56);
        tail.lineTo(28, 44);
        p.drawPath(tail);
        p.setPen(Qt::NoPen);
        p.setBrush(kAccent);
        for (int x : {20, 32, 44}) p.drawEllipse(QPointF(x, 26), 4, 4);
    };
    g["keep_selection"] = [](QPainter& p) {
        p.setPen(pen(kAccent, 4, Qt::DashLine));
        p.drawRect(QRectF(8, 8, 36, 36));
        p.setPen(pen(kLine, 4));
        p.drawPolyline(QPolygonF({QPointF(32, 30), QPointF(32, 58), QPointF(42, 50), QPointF(52, 58), QPointF(52, 30), QPointF(32, 30)}));
    };
    g["part.import_step"] = g["import"];
    g["part.export_step"] = [](QPainter& p) {
        p.setPen(pen(kLine));
        p.drawPolyline(QPolygonF({QPointF(8, 38), QPointF(8, 54), QPointF(56, 54), QPointF(56, 38)}));
        arrowUp(p, 32, 44, 8, kAccent);
    };
    g["part.export_mesh"] = g["part.export_step"];
    return g;
}

}  // namespace

QIcon toolIcon(const QString& name) {
    static const QHash<QString, Glyph> all = glyphs();
    static QHash<QString, QIcon> cache;
    if (auto it = cache.find(name); it != cache.end()) return *it;
    QIcon icon;
    for (int size : {32, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 64.0, size / 64.0);
        if (auto g = all.find(name); g != all.end()) {
            (*g)(p);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(58, 104, 150, 40));
            p.drawRoundedRect(QRectF(6, 6, 52, 52), 10, 10);
            p.setPen(kLine);
            QFont f = p.font();
            f.setPixelSize(22);
            f.setBold(true);
            p.setFont(f);
            const QString last = name.section('.', -1);
            p.drawText(QRectF(6, 6, 52, 52), Qt::AlignCenter, last.left(2).toUpper());
        }
        p.end();
        icon.addPixmap(pm);
    }
    cache.insert(name, icon);
    return icon;
}

}  // namespace orv

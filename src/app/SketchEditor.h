// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the sketch editor: a sketch's loops drawn in its plane over the
// section of a scan, to correct by hand what the automatic fit got wrong or
// draw what it missed. Drag vertices, the middle of an arc, a circle's centre
// or radius; turn lines into arcs and back; split and delete; draw
// rectangles, circles and polygons; type exact values. Each segment is
// coloured by how far the section strays from it.
#pragma once

#include "doc/Document.h"

#include <QDialog>
#include <QPointF>
#include <QWidget>

#include <optional>
#include <vector>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QDialogButtonBox;
class QActionGroup;

namespace orv {

/// What is picked in the editor.
struct SketchPick {
    enum class What { None, Vertex, Segment, ArcMiddle, Centre, Radius, Pole };
    What what{What::None};
    int loop{-1}, seg{-1};  // a vertex is the start of segment `seg`
    int index{-1};          // a pole of a spline (not its ends)
    bool operator==(const SketchPick& o) const { return what == o.what && loop == o.loop && seg == o.seg && index == o.index; }
};

class SketchCanvas : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Select, Rectangle, Circle, Polygon };

    explicit SketchCanvas(QWidget* parent = nullptr);

    void setProfile(const Profile& p);
    const Profile& profile() const { return prof_; }
    /// The scan's outline in the sketch's plane, drawn underneath and snapped to.
    void setReference(std::vector<std::vector<Vec2d>> polylines);
    void setTolerance(double t);
    void setTool(Tool t);
    void setSnap(bool on) { snap_ = on; }
    void fit();
    const SketchPick& pick() const { return pick_; }
    void setPick(const SketchPick& p);

    // Edits on the picked element; each is one step of the editor's undo.
    void lineToArc();
    void toSpline();
    void arcToLine();
    void split();
    void deleteVertex();
    void deleteLoop();
    void moveVertex(int loop, int seg, const Vec2d& to);
    void setArcRadius(double r);
    void setLineLength(double len);
    void setCircle(const Vec2d& centre, double radius);
    void movePole(int loop, int seg, int index, const Vec2d& to);
    void undo();
    void redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    /// How far the section strays from the sketch: worst and median, of the
    /// section points near a segment (farther ones are other outlines).
    struct Fit {
        double worst{0}, median{0};
        int points{0};
    };
    Fit fitToReference() const { return fit_; }

signals:
    void changed();      // the profile changed
    void picked();       // the pick changed
    void cursorMoved(const QPointF& mm);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QPointF toScreen(const Vec2d& p) const;
    Vec2d toPlane(const QPointF& p) const;
    SketchPick hitTest(const QPointF& screen) const;
    Vec2d snapped(const QPointF& screen) const;
    void pushUndo();
    void edited();
    void recomputeFit();
    void finishPolygon();

    Profile prof_;
    std::vector<std::vector<Vec2d>> reference_;
    double tolerance_{0.05};
    std::vector<double> segWorst_;  // per segment (flattened over loops): worst stray of the section from it
    std::vector<int> segOffset_;    // first index in segWorst_ of each loop
    Fit fit_;

    double scale_{10};              // pixels per mm
    Vec2d centre_{Vec2d::Zero()};   // mm at the middle of the widget
    Tool tool_{Tool::Select};
    bool snap_{true};
    SketchPick pick_, hover_;
    bool dragging_{false}, panning_{false};
    QPointF lastMouse_;
    std::vector<Vec2d> drawing_;    // points of the shape being drawn
    std::optional<Vec2d> cursor_;
    std::vector<Profile> undo_, redo_;
};

class SketchEditor : public QDialog {
    Q_OBJECT
public:
    SketchEditor(const Profile& profile, const QString& name, const std::vector<Body>& bodies, const QString& lang, QWidget* parent = nullptr);
    Profile profile() const;

private:
    void updatePanel();
    void updateStatus();
    void loadReference();

    SketchCanvas* canvas_{nullptr};
    QComboBox* scan_{nullptr};
    QDoubleSpinBox* tol_{nullptr};
    QCheckBox* snap_{nullptr};
    QFormLayout* form_{nullptr};
    QWidget* formBox_{nullptr};
    QLabel* status_{nullptr};
    QLabel* cursor_{nullptr};
    QDialogButtonBox* buttons_{nullptr};
    std::vector<Body> bodies_;
    bool updating_{false};
};

}  // namespace orv

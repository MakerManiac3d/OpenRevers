// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the sketch editor: a sketch's loops drawn in its plane over the
// section of a scan, to correct by hand what the automatic fit got wrong or
// draw what it missed. Drag vertices, the middle of an arc, a circle's centre
// or radius; turn lines into arcs and back; split and delete; draw
// rectangles, circles and polygons; type exact values. Each segment is
// coloured by how far the section strays from it. Geometric constraints and
// dimensions (PlaneGCS) keep the design intent: drags and edits are solved
// live, and a dimension may follow a parameter of the part.
#pragma once

#include "core/solid/SketchSolve.h"
#include "doc/Document.h"

#include <QDialog>
#include <QPointF>
#include <QWidget>

#include <map>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QDialogButtonBox;
class QActionGroup;
class QListWidget;

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

    // ---- constraints --------------------------------------------------
    /// The sketch's constraints, and the part's parameters that dimensions may name.
    void setConstraints(std::vector<SketchConstraint> c, std::map<std::string, double> parameters);
    const std::vector<SketchConstraint>& constraints() const { return cons_; }
    const std::map<std::string, double>& parameters() const { return params_; }
    int dof() const { return dof_; }
    bool solved() const { return solved_; }
    const QString& solveMessage() const { return solveMsg_; }
    /// The picked items, in the order they were picked (Shift or Ctrl adds).
    const std::vector<SketchPick>& selection() const { return selection_; }
    /// The selection as constraint references (picks that are not points or
    /// segments, like spline poles, are left out).
    std::vector<SketchRef> selectionRefs() const;
    /// Adds a constraint and solves. Returns why not when it cannot hold (the
    /// sketch is then unchanged).
    QString addConstraint(const SketchConstraint& c);
    void removeConstraint(int index);
    QString setConstraintValue(int index, double value, const std::string& parameter);
    int pickedConstraint() const { return pickedCons_; }
    void setPickedConstraint(int index);
    /// What a constraint measures now on the geometry.
    std::optional<double> measure(const SketchConstraint& c) const;
    void setAutoConstraints(bool on) { autoCons_ = on; }
    /// The short tag drawn by a constraint, and its kind's name.
    QString labelOf(const SketchConstraint& c) const;
    QString kindTitle(ConstraintKind k) const;
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
    /// Turns the picked loop into construction geometry (no material), or back.
    void toggleConstruction();
    /// Adds the sketch's origin (0), its x axis (1) or y axis (2) to the selection.
    void selectReference(int which);
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
    void constraintsChanged();
    void constraintPicked(int index);
    void constraintEditRequested(int index);

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
    /// Solves the constraints on `p` (dimensions that name a parameter take its value).
    bool solveOn(Profile& p, const SketchSolveOptions& opt, QString* why = nullptr, int* dof = nullptr) const;
    /// Solves the current sketch; keeps the geometry as it is if it cannot.
    void settle();
    /// Constraints that no longer fit after a loop's segments changed.
    void dropOnSegment(int loop, int seg);
    void dropOnLoop(int loop, bool renumber);
    void afterSplit(int loop, int seg);
    void addAutoConstraints(int loop);
    Vec2d anchorOf(const SketchRef& r) const;
    int constraintAt(const QPointF& screen) const;
    bool selected(const SketchPick& p) const;

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
    struct State {
        Profile profile;
        std::vector<SketchConstraint> constraints;
    };
    std::vector<State> undo_, redo_;

    std::vector<SketchConstraint> cons_;
    std::map<std::string, double> params_;
    int dof_{-1};
    bool solved_{true};
    QString solveMsg_;
    std::vector<SketchPick> selection_;
    int pickedCons_{-1};
    bool autoCons_{true};
    mutable std::vector<std::pair<QRectF, int>> tagRects_;  // where each constraint's tag was drawn
};

class SketchEditor : public QDialog {
    Q_OBJECT
public:
    SketchEditor(const Profile& profile, const QString& name, const std::vector<Body>& bodies, const QString& lang, QWidget* parent = nullptr,
                 const std::vector<SketchConstraint>& constraints = {}, const std::map<std::string, double>& parameters = {});
    Profile profile() const;
    std::vector<SketchConstraint> constraints() const;

private:
    void updatePanel();
    void updateStatus();
    void loadReference();
    void constrain(ConstraintKind kind);
    QString hint(ConstraintKind k) const;
    void updateConstraintList();
    void editConstraintValue(int index);
    /// A dimension's value as typed: a number, or the name of a parameter.
    bool readValue(const QString& text, double* value, std::string* parameter, QString* why) const;

    SketchCanvas* canvas_{nullptr};
    QComboBox* scan_{nullptr};
    QDoubleSpinBox* tol_{nullptr};
    QCheckBox* snap_{nullptr};
    QFormLayout* form_{nullptr};
    QWidget* formBox_{nullptr};
    QLabel* status_{nullptr};
    QLabel* cursor_{nullptr};
    QDialogButtonBox* buttons_{nullptr};
    QListWidget* consList_{nullptr};
    QLabel* dofLabel_{nullptr};
    QCheckBox* autoCons_{nullptr};
    std::vector<Body> bodies_;
    bool updating_{false};
};

}  // namespace orv

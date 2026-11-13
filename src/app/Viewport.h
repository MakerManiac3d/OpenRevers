// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the 3D view: shaded meshes with an optional wireframe and
// open-edge overlay, a turntable camera about the Z axis (CAD convention),
// standard views and an axis triad.
#pragma once

#include "doc/Document.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QPolygonF>
#include <QVector3D>

#include <map>
#include <utility>
#include <vector>
#include <memory>

namespace orv {

/// What a click hit: a point on a body's surface.
struct Pick {
    std::uint64_t body{0};       // 0: nothing hit
    Vec3d point{Vec3d::Zero()};  // mm
    Vec3d normal{Vec3d::Zero()};
    std::uint32_t face{0};
    int solidFace{-1};  // on a part: the face of its solid (-1: none)
    int solidEdge{-1};  // on a part: the edge of its solid near the click (-1: none)
    bool hit() const { return body != 0; }
};

/// What of a part is lit up: faces and edges picked, and sketches (the one
/// picked in the tree, or the one an operation uses).
struct PartHighlight {
    std::uint64_t body{0};
    std::vector<int> faces, edges;
    std::vector<std::uint64_t> sketches;
};

class Viewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    enum class View { Iso, Front, Back, Left, Right, Top, Bottom };

    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    void setBodies(const std::vector<Body>& bodies);
    void setSelected(std::uint64_t id);
    void setView(View v);
    void fitAll();
    void setWireframe(bool on);
    /// Colour the faces of bodies with found shapes by their region.
    void setShowShapes(bool on);
    /// Light up these regions of a body (the shapes selected in the panel).
    void setHighlightedRegions(std::uint64_t body, std::vector<std::int32_t> regions);
    /// Light up faces, edges and sketches of a part.
    void setPartHighlight(PartHighlight h);
    void setShowSketches(bool on);
    bool showSketches() const { return sketches_; }
    /// The point shown as picked (none: hit() false).
    void setPickMarker(const Pick& p);
    /// Lasso mode: a left drag draws a lasso (Alt+drag still turns the view)
    /// and selects the faces (or points) of a scan facing the view inside it.
    void setLassoMode(bool on);
    bool lassoMode() const { return lassoMode_; }
    /// The faces (or points of a cloud) selected, shown in red.
    void setSelection(std::uint64_t body, std::vector<std::uint32_t> faces);
    std::uint64_t selectionBody() const { return selBody_; }
    const std::vector<std::uint32_t>& selection() const { return selFaces_; }
    /// Numbered marks for aligning by pairs: on the moving body (orange)
    /// and on the reference (cyan).
    void setPairMarks(std::vector<Vec3d> moving, std::vector<Vec3d> reference);
    /// What a command would do (its preview): these bodies drawn as amber
    /// ghosts over the real ones. Empty: none.
    void setGhosts(std::vector<Body> ghosts);
    /// Handles a person drags in the view (a point of the command panel).
    struct Handle {
        QString id;
        Vec3d pos;
        QColor color;
    };
    void setHandles(std::vector<Handle> handles);
    /// Guide lines drawn over everything (a crop box, a sphere, a plane, a
    /// section's outline).
    struct Guide {
        Vec3d a, b;
        QColor color;
    };
    void setGuides(std::vector<Guide> guides);
    /// The points of a scan in progress, drawn over everything in amber (null:
    /// none). Only shown: not a body, not in the history.
    void setLiveCloud(std::shared_ptr<const TriMesh> cloud);
    /// The surface under a widget position (logical pixels).
    Pick pickAt(const QPoint& pos) const;
    bool showShapes() const { return shapes_; }
    void setOpenEdges(bool on);
    void setFlatShading(bool on);
    bool wireframe() const { return wireframe_; }
    bool openEdges() const { return openEdges_; }
    bool flatShading() const { return flat_; }
    /// The view as an image (for the agent and for reports).
    QImage capture(int width = 0, int height = 0);

    static View viewFromName(const QString& name, bool* ok);
    /// Empty when the 3D view works; otherwise why it does not.
    QString glError() const { return glError_; }

signals:
    void bodyClicked(std::uint64_t id);
    /// A click (no drag) with the left button; `add` when Ctrl was held.
    void picked(const orv::Pick& pick, bool add);
    /// A right click (no drag): the place for a context menu.
    void contextRequested(const orv::Pick& pick, const QPoint& globalPos);
    /// A lasso was drawn: what it selected (`add`: Ctrl was held).
    void lassoSelected(std::uint64_t body, std::vector<std::uint32_t> faces, bool add);
    /// A handle dragged to `pos` (`done`: the button was let go).
    void handleDragged(const QString& id, const orv::Vec3d& pos, bool done);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    struct GpuMesh {
        std::shared_ptr<const TriMesh> source;  // what was uploaded
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer ibo{QOpenGLBuffer::IndexBuffer};
        int indexCount{0};
        int pointCount{0};  // a point cloud: drawn as points, not triangles
        // Shapes view: one vertex per face corner, coloured by its region.
        QOpenGLVertexArrayObject regionVao;
        QOpenGLBuffer regionVbo{QOpenGLBuffer::VertexBuffer};
        int regionVertexCount{0};
        std::shared_ptr<const Features> regionSource;
        // A part: its solid's edges, its sketches, and the triangles of the faces lit up.
        QOpenGLVertexArrayObject partEdgeVao;
        QOpenGLBuffer partEdgeVbo{QOpenGLBuffer::VertexBuffer};
        int partEdgeVertexCount{0};
        std::shared_ptr<const PartView> partEdgeSource;
        QOpenGLVertexArrayObject sketchVao;
        QOpenGLBuffer sketchVbo{QOpenGLBuffer::VertexBuffer};
        int sketchVertexCount{0};
        QOpenGLVertexArrayObject litVao;
        QOpenGLBuffer litIbo{QOpenGLBuffer::IndexBuffer};
        int litIndexCount{0};
        QOpenGLVertexArrayObject litEdgeVao;
        QOpenGLBuffer litEdgeVbo{QOpenGLBuffer::VertexBuffer};
        int litEdgeVertexCount{0};
        std::string partKey;  // what the sketch and lit buffers were built from
        // A scan coloured by its distance to a part, one colour per vertex.
        QOpenGLVertexArrayObject devVao;
        QOpenGLBuffer devVbo{QOpenGLBuffer::VertexBuffer};
        std::shared_ptr<const DeviationMap> devSource;
        std::vector<std::int32_t> regionHighlight;  // what the buffer was built with
        // A quad mesh draws its wireframe as the quads' edges, not the triangles'.
        QOpenGLVertexArrayObject wireVao;
        QOpenGLBuffer wireIbo{QOpenGLBuffer::IndexBuffer};
        int wireIndexCount{0};
        QOpenGLVertexArrayObject edgeVao;
        QOpenGLBuffer edgeVbo{QOpenGLBuffer::VertexBuffer};
        int edgeVertexCount{-1};  // -1: not computed yet
        // The open edges are found on a worker thread (a large scan takes
        // seconds) and uploaded at the next paint.
        bool edgePending{false};
        std::shared_ptr<std::vector<float>> edgeLines;
    };
    void upload(GpuMesh& g, const TriMesh& m);
    void uploadRegions(GpuMesh& g, const TriMesh& m, const Features* f);
    void uploadPart(GpuMesh& g, const Body& b);
    void uploadDeviation(GpuMesh& g, const TriMesh& m, const DeviationMap& d);
    void drawLegend();
    void drawViewCube();
    void requestOpenEdges(GpuMesh& g);
    void uploadOpenEdges(GpuMesh& g);
    // The view cube's faces as last drawn, for clicks (screen polygon, view).
    std::vector<std::pair<QPolygonF, View>> cubeFaces_;
    QRectF cubeArea_;
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projMatrix() const;
    QVector3D eye() const;
    void drawBackground();
    void drawTriad();
    Box3 sceneBounds() const;

    std::vector<Body> bodies_;
    std::map<std::uint64_t, std::unique_ptr<GpuMesh>> gpu_;
    std::uint64_t selected_{0};
    bool ready_{false};
    QString glError_;  // set when OpenGL 3.3 is not available: the view shows it instead

    QOpenGLShaderProgram meshProg_, lineProg_, bgProg_;
    QOpenGLVertexArrayObject bgVao_, triadVao_;
    QOpenGLBuffer triadVbo_{QOpenGLBuffer::VertexBuffer};

    // Turntable camera, Z up.
    QVector3D target_{0, 0, 0};
    float distance_{100.f};
    float yaw_{-45.f}, pitch_{30.f};
    float sceneRadius_{50.f};
    bool wireframe_{false}, openEdges_{true}, flat_{false}, shapes_{true}, sketches_{true};
    PartHighlight partHighlight_;
    std::uint64_t highlightBody_{0};
    std::vector<std::int32_t> highlightRegions_;
    Pick marker_;
    void lassoSelect(bool add);
    void drawOverlay(const QMatrix4x4& mvp);
    bool lassoMode_{false}, lassoing_{false};
    QVector<QPointF> lassoPath_;
    std::uint64_t selBody_{0};
    std::vector<std::uint32_t> selFaces_;
    const TriMesh* selMesh_{nullptr};  // the mesh the indices refer to
    bool selDirty_{false};
    int selCount_{0};
    bool selPoints_{false};
    QOpenGLVertexArrayObject selVao_;
    QOpenGLBuffer selVbo_{QOpenGLBuffer::VertexBuffer};
    std::vector<Vec3d> pairMoving_, pairReference_;
    std::vector<Body> ghosts_;
    std::vector<std::unique_ptr<GpuMesh>> ghostGpu_;
    bool ghostsDirty_{false};
    std::vector<Handle> handles_;
    std::vector<Guide> guides_;
    int dragHandle_{-1};
    Vec3d dragNormal_{Vec3d::UnitZ()};
    bool screenPoint(const Vec3d& p, QPointF& out) const;
    std::shared_ptr<const TriMesh> live_;
    std::unique_ptr<GpuMesh> liveGpu_;
    bool liveDirty_{false};
    QOpenGLVertexArrayObject markerVao_;
    QOpenGLBuffer markerVbo_{QOpenGLBuffer::VertexBuffer};

    QPoint lastPos_;
    Qt::MouseButton dragButton_{Qt::NoButton};
    bool dragged_{false};
};

}  // namespace orv

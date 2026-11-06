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
#include <QVector3D>

#include <map>
#include <memory>

namespace orv {

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
        QOpenGLVertexArrayObject edgeVao;
        QOpenGLBuffer edgeVbo{QOpenGLBuffer::VertexBuffer};
        int edgeVertexCount{-1};  // -1: not computed yet
    };
    void upload(GpuMesh& g, const TriMesh& m);
    void uploadOpenEdges(GpuMesh& g, const TriMesh& m);
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
    bool wireframe_{false}, openEdges_{true}, flat_{false};

    QPoint lastPos_;
    Qt::MouseButton dragButton_{Qt::NoButton};
    bool dragged_{false};
};

}  // namespace orv

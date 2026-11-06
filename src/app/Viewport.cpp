// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Viewport.h"

#include "core/mesh/EdgeMap.h"

#include <unordered_set>

#include <QFutureWatcher>
#include <QMouseEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <QOpenGLContext>
#include <QPainter>
#include <QPalette>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace orv {

namespace {

const char* kMeshVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMvp;
uniform mat4 uView;
out vec3 vNormal;
out vec3 vViewPos;
void main() {
    vec4 vp = uView * vec4(aPos, 1.0);
    vViewPos = vp.xyz;
    vNormal = mat3(uView) * aNormal;
    gl_Position = uMvp * vec4(aPos, 1.0);
})";

const char* kMeshFs = R"(#version 330 core
in vec3 vNormal;
in vec3 vViewPos;
uniform vec3 uColor;
uniform bool uFlat;
uniform bool uPoints;
uniform float uHighlight;
out vec4 fragColor;
void main() {
    if (uPoints && dot(vNormal, vNormal) < 0.25) {  // points without normals: unlit
        fragColor = vec4(mix(uColor * 0.9, vec3(1.0, 0.72, 0.25), uHighlight * 0.18), 1.0);
        return;
    }
    vec3 n = uFlat && !uPoints ? normalize(cross(dFdx(vViewPos), dFdy(vViewPos))) : normalize(vNormal);
    vec3 v = normalize(-vViewPos);
    if (dot(n, v) < 0.0) n = -n;               // back faces lit like front faces
    bool back = !gl_FrontFacing;
    vec3 l1 = normalize(vec3(0.35, 0.55, 1.0)); // key light, from the viewer's upper left
    vec3 l2 = normalize(vec3(-0.6, -0.2, 0.4)); // fill light
    float diff = max(dot(n, l1), 0.0) * 0.75 + max(dot(n, l2), 0.0) * 0.25;
    vec3 h = normalize(l1 + v);
    float spec = pow(max(dot(n, h), 0.0), 48.0) * 0.25;
    vec3 base = back ? vec3(0.78, 0.45, 0.40) : uColor;  // inside of the surface in a warm tone
    vec3 c = base * (0.22 + 0.78 * diff) + vec3(spec);
    c = mix(c, vec3(1.0, 0.72, 0.25), uHighlight * 0.18);
    fragColor = vec4(c, 1.0);
})";

const char* kLineVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMvp;
out vec3 vColor;
void main() { vColor = aColor; gl_Position = uMvp * vec4(aPos, 1.0); })";

const char* kLineFs = R"(#version 330 core
in vec3 vColor;
uniform vec4 uTint;
out vec4 fragColor;
void main() { fragColor = uTint.a > 0.0 ? uTint : vec4(vColor, 1.0); })";

const char* kBgVs = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.999, 1.0);
})";

const char* kBgFs = R"(#version 330 core
in vec2 vUv;
uniform vec3 uTop;
uniform vec3 uBottom;
out vec4 fragColor;
void main() { fragColor = vec4(mix(uBottom, uTop, vUv.y), 1.0); })";

QVector3D toQ(const Vec3f& v) { return {v.x(), v.y(), v.z()}; }

}  // namespace

Viewport::Viewport(QWidget* parent) : QOpenGLWidget(parent) {
    QSurfaceFormat f = format();
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(24);
    f.setSamples(4);
    setFormat(f);
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
}

Viewport::~Viewport() {
    makeCurrent();
    gpu_.clear();
    doneCurrent();
}

void Viewport::initializeGL() {
    const QSurfaceFormat got = context()->format();
    if (got.version() < qMakePair(3, 3) || !initializeOpenGLFunctions()) {
        glError_ = tr("The 3D view needs OpenGL 3.3, and this computer offers %1.%2. Update the graphics driver; "
                      "on a computer without a graphics card, start OpenRevers with QT_OPENGL=software.")
                       .arg(got.majorVersion())
                       .arg(got.minorVersion());
        qWarning("OpenRevers: %s", qPrintable(glError_));
        return;
    }
    {
        auto str = [this](GLenum e) { return QString::fromLatin1(reinterpret_cast<const char*>(glGetString(e))); };
        qInfo("OpenRevers: OpenGL %s, %s", qPrintable(str(GL_VERSION)), qPrintable(str(GL_RENDERER)));
    }
    meshProg_.addShaderFromSourceCode(QOpenGLShader::Vertex, kMeshVs);
    meshProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, kMeshFs);
    meshProg_.link();
    lineProg_.addShaderFromSourceCode(QOpenGLShader::Vertex, kLineVs);
    lineProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, kLineFs);
    lineProg_.link();
    bgProg_.addShaderFromSourceCode(QOpenGLShader::Vertex, kBgVs);
    bgProg_.addShaderFromSourceCode(QOpenGLShader::Fragment, kBgFs);
    bgProg_.link();
    bgVao_.create();

    // Axis triad: X red, Y green, Z blue.
    const float triad[] = {0, 0, 0, 0.85f, 0.25f, 0.25f, 1, 0, 0, 0.85f, 0.25f, 0.25f, 0, 0, 0, 0.30f, 0.70f, 0.30f,
                           0, 1, 0, 0.30f, 0.70f, 0.30f, 0, 0, 0, 0.25f, 0.45f, 0.90f, 0, 0, 1, 0.25f, 0.45f, 0.90f};
    triadVao_.create();
    triadVao_.bind();
    triadVbo_.create();
    triadVbo_.bind();
    triadVbo_.allocate(triad, sizeof(triad));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    triadVao_.release();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    ready_ = true;
    setBodies(bodies_);
}

void Viewport::resizeGL(int, int) {}

void Viewport::upload(GpuMesh& g, const TriMesh& m) {
    const auto normals = !m.isPointCloud() ? m.vertexNormals()
                         : m.hasNormals()   ? m.normals
                                            : std::vector<Vec3f>(m.vertices.size(), Vec3f::Zero());
    std::vector<float> buf;
    buf.reserve(m.vertices.size() * 6);
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        buf.insert(buf.end(), {m.vertices[i].x(), m.vertices[i].y(), m.vertices[i].z(), normals[i].x(), normals[i].y(), normals[i].z()});
    }
    if (!g.vao.isCreated()) g.vao.create();
    g.vao.bind();
    if (!g.vbo.isCreated()) g.vbo.create();
    g.vbo.bind();
    g.vbo.allocate(buf.data(), static_cast<int>(buf.size() * sizeof(float)));
    if (!g.ibo.isCreated()) g.ibo.create();
    g.ibo.bind();
    g.ibo.allocate(m.faces.data(), static_cast<int>(m.faces.size() * sizeof(Tri)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    g.vao.release();
    g.indexCount = static_cast<int>(m.faces.size() * 3);
    g.pointCount = m.isPointCloud() ? static_cast<int>(m.vertices.size()) : 0;
    g.edgeVertexCount = -1;
    g.edgePending = false;
    g.edgeLines.reset();

    g.wireIndexCount = 0;
    if (m.hasQuads()) {
        std::unordered_set<std::uint64_t> twin;  // directed edges b->a present
        twin.reserve(m.quads.size() * 4);
        for (const Quad& q : m.quads)
            for (std::size_t k = 0; k < 4; ++k) twin.insert((std::uint64_t{q[(k + 1) % 4]} << 32) | q[k]);
        std::vector<std::uint32_t> lines;
        lines.reserve(m.quads.size() * 4);
        for (const Quad& q : m.quads)
            for (std::size_t k = 0; k < 4; ++k) {
                // Each inner edge belongs to two quads; draw it from one side only
                // (an open rim's edge, from its only quad).
                const auto a = q[k], b = q[(k + 1) % 4];
                if (a < b || !twin.count((std::uint64_t{a} << 32) | b)) { lines.push_back(a); lines.push_back(b); }
            }
        if (!g.wireVao.isCreated()) g.wireVao.create();
        g.wireVao.bind();
        g.vbo.bind();
        if (!g.wireIbo.isCreated()) g.wireIbo.create();
        g.wireIbo.bind();
        g.wireIbo.allocate(lines.data(), static_cast<int>(lines.size() * sizeof(std::uint32_t)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
        g.wireVao.release();
        g.wireIndexCount = static_cast<int>(lines.size());
    }
}

namespace {

/// Line vertices (position, colour) of the edges that are not shared by
/// exactly two faces: open rims red, edges with three or more faces magenta.
std::vector<float> openEdgeLines(const TriMesh& m) {
    const EdgeMap em(m);
    std::vector<float> buf;
    for (const auto& e : em.edges()) {
        if (e.count == 2) continue;
        const float r = 0.92f, gr = e.count == 1 ? 0.22f : 0.10f, b = e.count == 1 ? 0.18f : 0.85f;
        for (auto v : {e.a, e.b}) buf.insert(buf.end(), {m.vertices[v].x(), m.vertices[v].y(), m.vertices[v].z(), r, gr, b});
    }
    return buf;
}

}  // namespace

void Viewport::requestOpenEdges(GpuMesh& g) {
    if (g.edgePending || !g.source) return;
    g.edgePending = true;
    std::shared_ptr<const TriMesh> src = g.source;
    auto* watcher = new QFutureWatcher<std::vector<float>>(this);
    connect(watcher, &QFutureWatcher<std::vector<float>>::finished, this, [this, watcher, src]() {
        auto lines = std::make_shared<std::vector<float>>(watcher->result());
        watcher->deleteLater();
        for (auto& [id, gm] : gpu_)
            if (gm->source == src && gm->edgePending) gm->edgeLines = lines;  // still the same mesh
        update();
    });
    watcher->setFuture(QtConcurrent::run([src]() { return openEdgeLines(*src); }));
}

void Viewport::uploadOpenEdges(GpuMesh& g) {
    const std::vector<float>& buf = *g.edgeLines;
    if (!g.edgeVao.isCreated()) g.edgeVao.create();
    g.edgeVao.bind();
    if (!g.edgeVbo.isCreated()) g.edgeVbo.create();
    g.edgeVbo.bind();
    g.edgeVbo.allocate(buf.data(), static_cast<int>(buf.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    g.edgeVao.release();
    g.edgeVertexCount = static_cast<int>(buf.size() / 6);
    g.edgeLines.reset();
    g.edgePending = false;
}

void Viewport::setBodies(const std::vector<Body>& bodies) {
    const bool wasEmpty = bodies_.empty();
    bodies_ = bodies;
    if (!ready_) return;
    makeCurrent();
    std::map<std::uint64_t, std::unique_ptr<GpuMesh>> next;
    for (const auto& b : bodies_) {
        auto it = gpu_.find(b.id);
        std::unique_ptr<GpuMesh> g = it != gpu_.end() ? std::move(it->second) : std::make_unique<GpuMesh>();
        if (g->source != b.mesh) {
            upload(*g, *b.mesh);
            g->source = b.mesh;
        }
        next[b.id] = std::move(g);
    }
    gpu_ = std::move(next);  // meshes of removed bodies are freed here
    doneCurrent();
    const Box3 bb = sceneBounds();
    sceneRadius_ = std::max(1.f, bb.diagonal() * 0.5f);
    if (wasEmpty && !bodies_.empty()) fitAll();
    update();
}

void Viewport::setSelected(std::uint64_t id) {
    selected_ = id;
    update();
}

Box3 Viewport::sceneBounds() const {
    Box3 bb;
    for (const auto& b : bodies_) {
        if (!b.visible) continue;
        const Box3 x = b.mesh->bounds();
        if (!x.empty()) {
            bb.add(x.min);
            bb.add(x.max);
        }
    }
    return bb;
}

void Viewport::fitAll() {
    const Box3 bb = sceneBounds();
    if (bb.empty()) return;
    target_ = toQ(bb.center());
    sceneRadius_ = std::max(1e-3f, bb.diagonal() * 0.5f);
    distance_ = sceneRadius_ / std::tan(qDegreesToRadians(30.f) * 0.5f) * 1.1f;
    update();
}

Viewport::View Viewport::viewFromName(const QString& name, bool* ok) {
    static const std::map<QString, View> names{{"iso", View::Iso},   {"front", View::Front}, {"back", View::Back},     {"left", View::Left},
                                               {"right", View::Right}, {"top", View::Top},   {"bottom", View::Bottom}};
    auto it = names.find(name.toLower());
    if (ok) *ok = it != names.end();
    return it != names.end() ? it->second : View::Iso;
}

void Viewport::setView(View v) {
    switch (v) {
        case View::Iso: yaw_ = -45; pitch_ = 30; break;
        case View::Front: yaw_ = -90; pitch_ = 0; break;   // looking along +Y
        case View::Back: yaw_ = 90; pitch_ = 0; break;
        case View::Left: yaw_ = 180; pitch_ = 0; break;
        case View::Right: yaw_ = 0; pitch_ = 0; break;
        case View::Top: yaw_ = -90; pitch_ = 89.9f; break;
        case View::Bottom: yaw_ = -90; pitch_ = -89.9f; break;
    }
    fitAll();
}

void Viewport::setWireframe(bool on) { wireframe_ = on; update(); }
void Viewport::setOpenEdges(bool on) { openEdges_ = on; update(); }
void Viewport::setFlatShading(bool on) { flat_ = on; update(); }

QVector3D Viewport::eye() const {
    const float y = qDegreesToRadians(yaw_), p = qDegreesToRadians(pitch_);
    const QVector3D dir(std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p));
    return target_ + dir * distance_;
}

QMatrix4x4 Viewport::viewMatrix() const {
    QMatrix4x4 v;
    v.lookAt(eye(), target_, QVector3D(0, 0, 1));
    return v;
}

QMatrix4x4 Viewport::projMatrix() const {
    QMatrix4x4 p;
    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.f;
    const float nearZ = std::max(distance_ - sceneRadius_ * 4.f, distance_ * 1e-3f);
    const float farZ = distance_ + sceneRadius_ * 4.f;
    p.perspective(30.f, aspect, nearZ, farZ);
    return p;
}

void Viewport::drawBackground() {
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    glDisable(GL_DEPTH_TEST);
    bgProg_.bind();
    bgProg_.setUniformValue("uTop", dark ? QVector3D(0.20f, 0.22f, 0.25f) : QVector3D(0.86f, 0.89f, 0.93f));
    bgProg_.setUniformValue("uBottom", dark ? QVector3D(0.10f, 0.11f, 0.12f) : QVector3D(0.62f, 0.66f, 0.71f));
    bgVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    bgVao_.release();
    glEnable(GL_DEPTH_TEST);
}

void Viewport::drawTriad() {
    const int size = 90;
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, static_cast<int>(size * dpr), static_cast<int>(size * dpr));
    glClear(GL_DEPTH_BUFFER_BIT);
    QMatrix4x4 v;
    const float y = qDegreesToRadians(yaw_), p = qDegreesToRadians(pitch_);
    v.lookAt(QVector3D(std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)) * 3.f, QVector3D(0, 0, 0), QVector3D(0, 0, 1));
    QMatrix4x4 o;
    o.ortho(-1.4f, 1.4f, -1.4f, 1.4f, 0.1f, 10.f);
    lineProg_.bind();
    lineProg_.setUniformValue("uMvp", o * v);
    lineProg_.setUniformValue("uTint", QVector4D(0, 0, 0, 0));
    triadVao_.bind();
    glLineWidth(2.f);
    glDrawArrays(GL_LINES, 0, 6);
    glLineWidth(1.f);
    triadVao_.release();
    glViewport(0, 0, static_cast<int>(width() * dpr), static_cast<int>(height() * dpr));
}

void Viewport::paintGL() {
    if (!glError_.isEmpty()) {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Window));
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(rect().adjusted(40, 40, -40, -40), Qt::AlignCenter | Qt::TextWordWrap, glError_);
        return;
    }
    if (!ready_) return;
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, static_cast<int>(width() * dpr), static_cast<int>(height() * dpr));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawBackground();
    const QMatrix4x4 view = viewMatrix(), mvp = projMatrix() * view;

    meshProg_.bind();
    meshProg_.setUniformValue("uMvp", mvp);
    meshProg_.setUniformValue("uView", view);
    meshProg_.setUniformValue("uFlat", flat_);
    meshProg_.setUniformValue("uPoints", false);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.f, 1.f);
    for (const auto& b : bodies_) {
        if (!b.visible) continue;
        auto& g = *gpu_.at(b.id);
        meshProg_.setUniformValue("uColor", QVector3D(b.color[0], b.color[1], b.color[2]));
        // Tint the selected body only when there is another to tell it from.
        meshProg_.setUniformValue("uHighlight", b.id == selected_ && bodies_.size() > 1 ? 1.f : 0.f);
        g.vao.bind();
        if (g.pointCount > 0) {
            meshProg_.setUniformValue("uPoints", true);
            glPointSize(2.f);
            glDrawArrays(GL_POINTS, 0, g.pointCount);
            meshProg_.setUniformValue("uPoints", false);
        } else {
            glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_INT, nullptr);
        }
        g.vao.release();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    lineProg_.bind();
    lineProg_.setUniformValue("uMvp", mvp);
    for (const auto& b : bodies_) {
        if (!b.visible) continue;
        auto& g = *gpu_.at(b.id);
        if (wireframe_) {
            lineProg_.setUniformValue("uTint", QVector4D(0.12f, 0.14f, 0.16f, 0.6f));
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (g.wireIndexCount > 0) {
                g.wireVao.bind();
                glDrawElements(GL_LINES, g.wireIndexCount, GL_UNSIGNED_INT, nullptr);
                g.wireVao.release();
            } else {
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                g.vao.bind();
                glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_INT, nullptr);
                g.vao.release();
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
            glDisable(GL_BLEND);
        }
        if (openEdges_) {
            if (g.edgeLines) uploadOpenEdges(g);
            else if (g.edgeVertexCount < 0) requestOpenEdges(g);
            if (g.edgeVertexCount > 0) {
                lineProg_.setUniformValue("uTint", QVector4D(0, 0, 0, 0));
                glLineWidth(2.f);
                g.edgeVao.bind();
                glDrawArrays(GL_LINES, 0, g.edgeVertexCount);
                g.edgeVao.release();
                glLineWidth(1.f);
            }
        }
    }
    drawTriad();
}

QImage Viewport::capture(int w, int h) {
    QImage img = grabFramebuffer();
    if (w > 0 && h > 0 && !img.isNull()) img = img.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

void Viewport::mousePressEvent(QMouseEvent* e) {
    lastPos_ = e->position().toPoint();
    dragButton_ = e->button();
    dragged_ = false;
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
    const QPoint p = e->position().toPoint();
    const QPoint d = p - lastPos_;
    lastPos_ = p;
    if (d.manhattanLength() > 0) dragged_ = true;
    const bool orbit = dragButton_ == Qt::LeftButton && !(e->modifiers() & Qt::ShiftModifier);
    const bool pan = dragButton_ == Qt::MiddleButton || dragButton_ == Qt::RightButton || (dragButton_ == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier));
    if (orbit) {
        yaw_ -= static_cast<float>(d.x()) * 0.4f;
        pitch_ = std::clamp(pitch_ + static_cast<float>(d.y()) * 0.4f, -89.9f, 89.9f);
        update();
    } else if (pan) {
        const QMatrix4x4 v = viewMatrix();
        const QVector3D right(v(0, 0), v(0, 1), v(0, 2)), up(v(1, 0), v(1, 1), v(1, 2));
        const float scale = 2.f * distance_ * std::tan(qDegreesToRadians(15.f)) / static_cast<float>(std::max(1, height()));
        target_ -= right * static_cast<float>(d.x()) * scale;
        target_ += up * static_cast<float>(d.y()) * scale;
        update();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragged_ && e->button() == Qt::LeftButton) emit bodyClicked(0);
    dragButton_ = Qt::NoButton;
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) fitAll();
}

void Viewport::wheelEvent(QWheelEvent* e) {
    const float steps = static_cast<float>(e->angleDelta().y()) / 120.f;
    distance_ = std::clamp(distance_ * std::pow(0.88f, steps), sceneRadius_ * 0.01f, sceneRadius_ * 200.f);
    update();
}

}  // namespace orv

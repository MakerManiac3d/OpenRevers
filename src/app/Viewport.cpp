// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Viewport.h"

#include <QPolygonF>

#include "core/mesh/EdgeMap.h"

#include <unordered_set>

#include <QColor>
#include <QLocale>
#include <QFutureWatcher>
#include <QMouseEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <QOpenGLContext>
#include <QPainter>
#include <QPalette>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace orv {

namespace {

const char* kMeshVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
uniform mat4 uMvp;
uniform mat4 uView;
out vec3 vNormal;
out vec3 vViewPos;
out vec3 vColor;
void main() {
    vColor = aColor;
    vec4 vp = uView * vec4(aPos, 1.0);
    vViewPos = vp.xyz;
    vNormal = mat3(uView) * aNormal;
    gl_Position = uMvp * vec4(aPos, 1.0);
})";

const char* kMeshFs = R"(#version 330 core
in vec3 vNormal;
in vec3 vViewPos;
in vec3 vColor;
uniform vec3 uColor;
uniform bool uVertexColor;  // shapes view: each face coloured by its region
uniform bool uFlat;
uniform bool uPoints;
uniform float uHighlight;
uniform float uAlpha;
out vec4 fragColor;
void main() {
    if (uPoints && dot(vNormal, vNormal) < 0.25) {  // points without normals: unlit
        fragColor = vec4(mix(uColor * 0.9, vec3(1.0, 0.72, 0.25), uHighlight * 0.18), uAlpha);
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
    // The inside of a surface in a warm tone; coloured by vertex (deviation,
    // shapes), both sides keep the colour, the back a little darker: a scan
    // of one side seen from behind must still show its deviation.
    vec3 base = uVertexColor ? vColor * (back ? 0.85 : 1.0) : (back ? vec3(0.78, 0.45, 0.40) : uColor);
    vec3 c = base * (0.22 + 0.78 * diff) + vec3(spec);
    c = mix(c, vec3(1.0, 0.72, 0.25), uHighlight * 0.18);
    fragColor = vec4(c, uAlpha);
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
    liveGpu_.reset();
    ghostGpu_.clear();
    selVbo_.destroy();
    selVao_.destroy();
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
    // A selection refers to faces of one mesh: gone when that mesh changes.
    if (selBody_) {
        bool same = false;
        for (const auto& b : bodies_) same |= b.id == selBody_ && b.mesh.get() == selMesh_;
        if (!same) {
            selBody_ = 0;
            selFaces_.clear();
            selMesh_ = nullptr;
            selCount_ = 0;
        }
    }
    if (!ready_) return;
    makeCurrent();
    std::map<std::uint64_t, std::unique_ptr<GpuMesh>> next;
    for (const auto& b : bodies_) {
        auto it = gpu_.find(b.id);
        std::unique_ptr<GpuMesh> g = it != gpu_.end() ? std::move(it->second) : std::make_unique<GpuMesh>();
        if (g->source != b.mesh) {
            upload(*g, *b.mesh);
            g->source = b.mesh;
            g->regionSource.reset();
            g->regionVertexCount = 0;
        }
        const std::vector<std::int32_t> hl = b.id == highlightBody_ ? highlightRegions_ : std::vector<std::int32_t>{};
        if (g->regionSource != b.features || g->regionHighlight != hl) {
            g->regionHighlight = hl;
            uploadRegions(*g, *b.mesh, b.features.get());
            g->regionSource = b.features;
        }
        if (b.isPart()) uploadPart(*g, b);
        if (g->devSource != b.deviation) {
            if (b.deviation && b.deviation->distance.size() == b.mesh->vertices.size()) uploadDeviation(*g, *b.mesh, *b.deviation);
            g->devSource = b.deviation;
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

void Viewport::setLiveCloud(std::shared_ptr<const TriMesh> cloud) {
    const bool first = !live_ && cloud && !cloud->vertices.empty() && bodies_.empty();
    live_ = std::move(cloud);
    liveDirty_ = true;
    if (first) fitAll();
    update();
}

Box3 Viewport::sceneBounds() const {
    Box3 bb;
    if (live_ && !live_->vertices.empty()) {
        const Box3 x = live_->bounds();
        bb.add(x.min);
        bb.add(x.max);
    }
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
void Viewport::setShowShapes(bool on) { shapes_ = on; update(); }

namespace {

/// Colour of a region: hue by shape type, lightness varied per region so
/// that neighbours of one type can be told apart.
QVector3D regionColor(PrimitiveType t, std::int32_t region) {
    float h = 0.58f;  // plane: blue
    switch (t) {
        case PrimitiveType::Plane: h = 0.58f; break;
        case PrimitiveType::Cylinder: h = 0.33f; break;  // green
        case PrimitiveType::Cone: h = 0.08f; break;      // orange
        case PrimitiveType::Sphere: h = 0.80f; break;    // violet
        case PrimitiveType::Torus: h = 0.15f; break;     // yellow
    }
    const std::uint32_t k = static_cast<std::uint32_t>(region) * 2654435761u;
    const float l = 0.38f + 0.36f * static_cast<float>((k >> 8) % 100) / 100.f;
    const float hue = h + 0.12f * (static_cast<float>((k >> 16) % 100) / 100.f - 0.5f);
    const QColor c = QColor::fromHslF(std::fmod(hue + 1.f, 1.f), 0.6f, l);
    return {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF())};
}

}  // namespace

void Viewport::uploadRegions(GpuMesh& g, const TriMesh& m, const Features* f) {
    g.regionVertexCount = 0;
    if (!f || f->faceRegion.size() != m.faces.size() || m.faces.empty()) return;
    std::vector<PrimitiveType> typeOf;
    for (const auto& x : f->list)
        if (x.region >= 0) {
            if (typeOf.size() <= static_cast<std::size_t>(x.region)) typeOf.resize(static_cast<std::size_t>(x.region) + 1, PrimitiveType::Plane);
            typeOf[static_cast<std::size_t>(x.region)] = x.shape.type;
        }
    std::vector<float> buf;
    buf.reserve(m.faces.size() * 27);
    for (std::size_t i = 0; i < m.faces.size(); ++i) {
        const std::int32_t r = f->faceRegion[i];
        const Vec3f fn = m.faceNormal(i);  // flat per face: the regions' borders stay crisp
        QVector3D c(0.55f, 0.55f, 0.55f);  // not in any region
        if (r >= 0 && static_cast<std::size_t>(r) < typeOf.size()) c = regionColor(typeOf[static_cast<std::size_t>(r)], r);
        if (r >= 0 && std::find(g.regionHighlight.begin(), g.regionHighlight.end(), r) != g.regionHighlight.end())
            c = QVector3D(1.0f, 0.78f, 0.25f);  // selected: amber
        for (auto v : m.faces[i])
            buf.insert(buf.end(), {m.vertices[v].x(), m.vertices[v].y(), m.vertices[v].z(), fn.x(), fn.y(), fn.z(), c.x(), c.y(), c.z()});
    }
    if (!g.regionVao.isCreated()) g.regionVao.create();
    g.regionVao.bind();
    if (!g.regionVbo.isCreated()) g.regionVbo.create();
    g.regionVbo.bind();
    g.regionVbo.allocate(buf.data(), static_cast<int>(buf.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(24));
    g.regionVao.release();
    g.regionVertexCount = static_cast<int>(m.faces.size() * 3);
}

namespace {

/// Colour of a distance to a part: green within the tolerance, then yellow
/// to red where the scan lies outside the part, cyan to blue inside it.
QVector3D deviationColor(float d, float tol, float range) {
    const float a = std::abs(d);
    if (a <= tol) return {0.30f, 0.75f, 0.35f};
    const float t = std::clamp((a - tol) / std::max(1e-6f, range - tol), 0.f, 1.f);
    if (d > 0) return t < 0.5f ? QVector3D(0.30f + 1.30f * t, 0.75f + 0.20f * t, 0.35f - 0.5f * t) : QVector3D(0.95f, 0.85f - 1.4f * (t - 0.5f), 0.10f);
    return t < 0.5f ? QVector3D(0.30f - 0.4f * t, 0.75f + 0.10f * t, 0.35f + 1.2f * t) : QVector3D(0.10f, 0.80f - 1.2f * (t - 0.5f), 0.95f);
}

}  // namespace

void Viewport::uploadDeviation(GpuMesh& g, const TriMesh& m, const DeviationMap& d) {
    const auto normals = m.vertexNormals();
    std::vector<float> buf;
    buf.reserve(m.vertices.size() * 9);
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const QVector3D c = deviationColor(d.distance[i], d.tolerance, d.range);
        buf.insert(buf.end(), {m.vertices[i].x(), m.vertices[i].y(), m.vertices[i].z(), normals[i].x(), normals[i].y(), normals[i].z(), c.x(), c.y(), c.z()});
    }
    if (!g.devVao.isCreated()) g.devVao.create();
    g.devVao.bind();
    if (!g.devVbo.isCreated()) g.devVbo.create();
    g.devVbo.bind();
    g.devVbo.allocate(buf.data(), static_cast<int>(buf.size() * sizeof(float)));
    g.ibo.bind();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(24));
    g.devVao.release();
}

void Viewport::uploadPart(GpuMesh& g, const Body& b) {
    auto lines = [this](QOpenGLVertexArrayObject& vao, QOpenGLBuffer& vbo, const std::vector<float>& buf) {
        if (!vao.isCreated()) vao.create();
        vao.bind();
        if (!vbo.isCreated()) vbo.create();
        vbo.bind();
        vbo.allocate(buf.data(), static_cast<int>(buf.size() * sizeof(float)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
        vao.release();
        return static_cast<int>(buf.size() / 6);
    };
    auto addPolyline = [](std::vector<float>& buf, const std::vector<Vec3f>& pts, QVector3D c) {
        for (std::size_t k = 0; k + 1 < pts.size(); ++k)
            for (const auto& p : {pts[k], pts[k + 1]}) buf.insert(buf.end(), {p.x(), p.y(), p.z(), c.x(), c.y(), c.z()});
    };
    // The solid's edges, when the solid changes.
    if (g.partEdgeSource != b.view) {
        std::vector<float> buf;
        if (b.view)
            for (const auto& e : b.view->edges) addPolyline(buf, e, {0.10f, 0.12f, 0.15f});
        g.partEdgeVertexCount = lines(g.partEdgeVao, g.partEdgeVbo, buf);
        g.partEdgeSource = b.view;
    }
    // The sketches and what is lit up, when the model or the highlight change.
    const bool mine = partHighlight_.body == b.id;
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(b.model.get())) + "/" + std::to_string(reinterpret_cast<std::uintptr_t>(b.view.get()));
    if (mine) {
        for (int f : partHighlight_.faces) key += "f" + std::to_string(f);
        for (int e : partHighlight_.edges) key += "e" + std::to_string(e);
        for (auto sk : partHighlight_.sketches) key += "s" + std::to_string(sk);
    }
    if (g.partKey == key) return;
    g.partKey = key;
    std::vector<float> sk;
    const float step = std::max(0.02f, sceneRadius_ * 0.004f);
    for (const auto& sketch : b.model->sketches) {
        const bool lit = mine && std::find(partHighlight_.sketches.begin(), partHighlight_.sketches.end(), sketch.id) != partHighlight_.sketches.end();
        const QVector3D c = lit ? QVector3D(1.0f, 0.55f, 0.10f) : QVector3D(0.20f, 0.45f, 0.95f);
        SketchPlane pl = sketch.profile.plane;
        pl.normalize();
        for (const auto& loop : sketch.profile.loops) {
            auto poly = loop.polygon(step);
            if (poly.empty()) continue;
            poly.push_back(poly.front());
            std::vector<Vec3f> pts;
            for (const auto& q : poly) pts.push_back(pl.toWorld(q).cast<float>());
            addPolyline(sk, pts, c);
        }
    }
    // Free surfaces: a grid of lines over them, violet (the picked one, orange).
    for (const auto& f : b.model->surfaces) {
        const bool lit = mine && std::find(partHighlight_.sketches.begin(), partHighlight_.sketches.end(), f.id) != partHighlight_.sketches.end();
        const QVector3D c = lit ? QVector3D(1.0f, 0.55f, 0.10f) : QVector3D(0.62f, 0.35f, 0.85f);
        for (const auto& line : f.surface.isoLines(9, 40)) addPolyline(sk, line, c);
    }
    g.sketchVertexCount = lines(g.sketchVao, g.sketchVbo, sk);
    std::vector<float> le;
    std::vector<std::uint32_t> lit;
    if (mine && b.view) {
        for (int e : partHighlight_.edges)
            if (e >= 0 && static_cast<std::size_t>(e) < b.view->edges.size()) addPolyline(le, b.view->edges[static_cast<std::size_t>(e)], {1.0f, 0.62f, 0.10f});
        for (std::size_t t = 0; t < b.view->faceOf.size() && t < b.mesh->faces.size(); ++t)
            if (std::find(partHighlight_.faces.begin(), partHighlight_.faces.end(), static_cast<int>(b.view->faceOf[t])) != partHighlight_.faces.end())
                lit.insert(lit.end(), b.mesh->faces[t].begin(), b.mesh->faces[t].end());
    }
    g.litEdgeVertexCount = lines(g.litEdgeVao, g.litEdgeVbo, le);
    if (!g.litVao.isCreated()) g.litVao.create();
    g.litVao.bind();
    g.vbo.bind();
    if (!g.litIbo.isCreated()) g.litIbo.create();
    g.litIbo.bind();
    g.litIbo.allocate(lit.data(), static_cast<int>(lit.size() * sizeof(std::uint32_t)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    g.litVao.release();
    g.litIndexCount = static_cast<int>(lit.size());
}

void Viewport::setPartHighlight(PartHighlight h) {
    partHighlight_ = std::move(h);
    const std::vector<Body> same = bodies_;
    setBodies(same);  // rebuilds only what changed
}

void Viewport::setShowSketches(bool on) { sketches_ = on; update(); }

void Viewport::drawViewCube() {
    // A cube in the top right corner turned like the view: a click on a face
    // looks from that side, a click beside it gives the isometric view.
    const QMatrix4x4 v = viewMatrix();
    const QPointF c(width() - 64.0, 64.0);
    const double s = 26.0;
    cubeArea_ = QRectF(c.x() - 52, c.y() - 52, 104, 104);
    struct Face { QVector3D n, u, w; View view; QString label; };
    const Face faces[] = {
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, View::Front, tr("Front")},
        {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, View::Back, tr("Back")},
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, View::Right, tr("Right")},
        {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, View::Left, tr("Left")},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, View::Top, tr("Top")},
        {{0, 0, -1}, {1, 0, 0}, {0, -1, 0}, View::Bottom, tr("Bottom")},
    };
    auto project = [&](const QVector3D& p3) {
        const QVector3D q = v.mapVector(p3);
        return QPointF(c.x() + q.x() * s, c.y() - q.y() * s);
    };
    struct Drawn { QPolygonF poly; double depth; const Face* f; double facing; };
    std::vector<Drawn> drawn;
    for (const auto& f : faces) {
        const double facing = v.mapVector(f.n).z();
        if (facing <= 0.02) continue;
        QPolygonF poly;
        for (auto [a, b] : {std::pair{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}}) poly << project(f.n + f.u * a + f.w * b);
        drawn.push_back({poly, v.mapVector(f.n).z(), &f, facing});
    }
    std::sort(drawn.begin(), drawn.end(), [](const Drawn& a, const Drawn& b) { return a.depth < b.depth; });
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    cubeFaces_.clear();
    QFont font = p.font();
    font.setPointSizeF(font.pointSizeF() * 0.75);
    font.setBold(true);
    p.setFont(font);
    for (const auto& d : drawn) {
        const int shade = 200 + static_cast<int>(40 * d.facing);
        p.setPen(QPen(QColor(70, 100, 130), 1.2));
        p.setBrush(QColor(shade, shade + 4 > 255 ? 255 : shade + 4, 255, 225));
        p.drawPolygon(d.poly);
        if (d.facing > 0.45) {
            p.setPen(QColor(40, 60, 85));
            p.drawText(d.poly.boundingRect(), Qt::AlignCenter, d.f->label);
        }
        cubeFaces_.emplace_back(d.poly, d.f->view);
    }
}

void Viewport::drawLegend() {
    const DeviationMap* d = nullptr;
    for (const auto& b : bodies_)
        if (b.visible && b.deviation) d = b.deviation.get();
    if (!d) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int w = 18, h = 220, x = width() - w - 70, y = 140;  // under the view cube
    for (int k = 0; k < h; ++k) {
        const float v = d->range * (1.f - 2.f * static_cast<float>(k) / static_cast<float>(h - 1));
        const QVector3D c = deviationColor(v, d->tolerance, d->range);
        p.setPen(QColor::fromRgbF(c.x(), c.y(), c.z()));
        p.drawLine(x, y + k, x + w, y + k);
    }
    p.setPen(palette().color(QPalette::WindowText));
    QFont f = p.font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    p.setFont(f);
    int lastY = -100;  // labels closer than a line apart are left out
    auto label = [&](float v) {
        const int yy = y + static_cast<int>((1.f - (v / d->range + 1.f) * 0.5f) * static_cast<float>(h - 1));
        if (yy - lastY < 13) return;
        lastY = yy;
        p.drawLine(x + w, yy, x + w + 4, yy);
        p.drawText(x + w + 7, yy + 4, (v > 0 ? "+" : "") + QLocale().toString(v, 'f', 2));
    };
    for (float v : {d->range, d->tolerance, -d->tolerance, -d->range}) label(v);
    p.drawText(x - 30, y + h + 16, tr("mm, scan − part"));
}

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
    meshProg_.setUniformValue("uVertexColor", false);
    meshProg_.setUniformValue("uAlpha", 1.f);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.f, 1.f);
    // A body a preview changes is seen through, so its ghost shows.
    auto ghosted = [&](const Body& b) {
        for (const auto& g : ghosts_)
            if (g.id == b.id) return true;
        return false;
    };
    auto opacity = [&](const Body& b) { return ghosted(b) ? std::min(b.opacity, 0.25f) : b.opacity; };
    auto drawBody = [&](const Body& b) {
        auto& g = *gpu_.at(b.id);
        meshProg_.setUniformValue("uColor", QVector3D(b.color[0], b.color[1], b.color[2]));
        meshProg_.setUniformValue("uAlpha", opacity(b));
        // Tint the selected body only when there is another to tell it from.
        meshProg_.setUniformValue("uHighlight", b.id == selected_ && bodies_.size() > 1 && !b.deviation ? 1.f : 0.f);
        if (b.deviation && g.devSource == b.deviation && g.devVao.isCreated()) {
            meshProg_.setUniformValue("uVertexColor", true);
            g.devVao.bind();
            glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_INT, nullptr);
            g.devVao.release();
            meshProg_.setUniformValue("uVertexColor", false);
            return;
        }
        if (shapes_ && g.regionVertexCount > 0) {
            meshProg_.setUniformValue("uVertexColor", true);
            g.regionVao.bind();
            glDrawArrays(GL_TRIANGLES, 0, g.regionVertexCount);
            g.regionVao.release();
            meshProg_.setUniformValue("uVertexColor", false);
            return;
        }
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
    };
    for (const auto& b : bodies_)
        if (b.visible && opacity(b) >= 1.f) drawBody(b);
    glDisable(GL_POLYGON_OFFSET_FILL);
    // A scan in progress: its points, in amber.
    if (liveDirty_) {
        liveDirty_ = false;
        if (live_ && !live_->vertices.empty()) {
            if (!liveGpu_) liveGpu_ = std::make_unique<GpuMesh>();
            upload(*liveGpu_, *live_);
        } else {
            liveGpu_.reset();
        }
    }
    if (liveGpu_ && liveGpu_->pointCount > 0) {
        meshProg_.setUniformValue("uColor", QVector3D(1.0f, 0.62f, 0.15f));
        meshProg_.setUniformValue("uAlpha", 1.f);
        meshProg_.setUniformValue("uHighlight", 0.f);
        meshProg_.setUniformValue("uPoints", true);
        glPointSize(2.f);
        liveGpu_->vao.bind();
        glDrawArrays(GL_POINTS, 0, liveGpu_->pointCount);
        liveGpu_->vao.release();
        meshProg_.setUniformValue("uPoints", false);
    }
    // Faces of a part picked: drawn again over themselves, in amber.
    meshProg_.setUniformValue("uAlpha", 1.f);
    meshProg_.setUniformValue("uHighlight", 0.f);
    for (const auto& b : bodies_) {
        if (!b.visible || !b.isPart()) continue;
        auto& g = *gpu_.at(b.id);
        if (g.litIndexCount <= 0) continue;
        meshProg_.setUniformValue("uColor", QVector3D(1.0f, 0.70f, 0.25f));
        glDepthFunc(GL_LEQUAL);
        g.litVao.bind();
        glDrawElements(GL_TRIANGLES, g.litIndexCount, GL_UNSIGNED_INT, nullptr);
        g.litVao.release();
        glDepthFunc(GL_LESS);
    }

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
        if (openEdges_ && !b.isPart()) {  // a part's faces are triangulated one by one: their seams are not holes
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
    // Parts: their solid's edges, the edges picked, and their sketches (over everything).
    lineProg_.bind();
    lineProg_.setUniformValue("uMvp", mvp);
    lineProg_.setUniformValue("uTint", QVector4D(0, 0, 0, 0));
    for (const auto& b : bodies_) {
        if (!b.visible || !b.isPart()) continue;
        auto& g = *gpu_.at(b.id);
        if (g.partEdgeVertexCount > 0) {
            glLineWidth(1.5f);
            g.partEdgeVao.bind();
            glDrawArrays(GL_LINES, 0, g.partEdgeVertexCount);
            g.partEdgeVao.release();
        }
        if (g.litEdgeVertexCount > 0) {
            glDisable(GL_DEPTH_TEST);
            glLineWidth(4.f);
            g.litEdgeVao.bind();
            glDrawArrays(GL_LINES, 0, g.litEdgeVertexCount);
            g.litEdgeVao.release();
            glEnable(GL_DEPTH_TEST);
        }
        if (sketches_ && g.sketchVertexCount > 0) {
            glDisable(GL_DEPTH_TEST);
            glLineWidth(2.f);
            g.sketchVao.bind();
            glDrawArrays(GL_LINES, 0, g.sketchVertexCount);
            g.sketchVao.release();
            glEnable(GL_DEPTH_TEST);
        }
        glLineWidth(1.f);
    }
    // Bodies seen through, last: blended over what is behind them.
    bool anySeeThrough = false;
    for (const auto& b : bodies_) anySeeThrough |= b.visible && opacity(b) < 1.f;
    if (anySeeThrough) {
        meshProg_.bind();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (const auto& b : bodies_)
            if (b.visible && opacity(b) < 1.f) drawBody(b);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        meshProg_.setUniformValue("uAlpha", 1.f);
    }
    // A preview: what the command would make, as amber ghosts.
    if (ghostsDirty_) {
        ghostsDirty_ = false;
        ghostGpu_.clear();
        for (const auto& g : ghosts_) {
            auto gm = std::make_unique<GpuMesh>();
            if (g.mesh && !g.mesh->vertices.empty()) upload(*gm, *g.mesh);
            ghostGpu_.push_back(std::move(gm));
        }
    }
    if (!ghostGpu_.empty()) {
        meshProg_.bind();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        meshProg_.setUniformValue("uColor", QVector3D(1.0f, 0.62f, 0.15f));
        meshProg_.setUniformValue("uAlpha", 0.5f);
        meshProg_.setUniformValue("uHighlight", 0.f);
        for (const auto& g : ghostGpu_) {
            if (g->indexCount == 0 && g->pointCount == 0) continue;
            g->vao.bind();
            if (g->pointCount > 0) {
                meshProg_.setUniformValue("uPoints", true);
                glPointSize(2.f);
                glDrawArrays(GL_POINTS, 0, g->pointCount);
                meshProg_.setUniformValue("uPoints", false);
            } else {
                glDrawElements(GL_TRIANGLES, g->indexCount, GL_UNSIGNED_INT, nullptr);
            }
            g->vao.release();
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        meshProg_.setUniformValue("uAlpha", 1.f);
    }
    if (marker_.hit()) {
        // A small 3-D cross at the picked point, drawn over everything.
        const float r = sceneRadius_ * 0.025f;
        const Vec3f p = marker_.point.cast<float>();
        const float m[] = {p.x() - r, p.y(), p.z(), 1, 0.55f, 0.1f, p.x() + r, p.y(), p.z(), 1, 0.55f, 0.1f,
                           p.x(), p.y() - r, p.z(), 1, 0.55f, 0.1f, p.x(), p.y() + r, p.z(), 1, 0.55f, 0.1f,
                           p.x(), p.y(), p.z() - r, 1, 0.55f, 0.1f, p.x(), p.y(), p.z() + r, 1, 0.55f, 0.1f};
        if (!markerVao_.isCreated()) markerVao_.create();
        markerVao_.bind();
        if (!markerVbo_.isCreated()) markerVbo_.create();
        markerVbo_.bind();
        markerVbo_.allocate(m, sizeof m);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
        lineProg_.bind();
        lineProg_.setUniformValue("uMvp", mvp);
        lineProg_.setUniformValue("uTint", QVector4D(0, 0, 0, 0));
        glDisable(GL_DEPTH_TEST);
        glLineWidth(3.f);
        glDrawArrays(GL_LINES, 0, 6);
        glLineWidth(1.f);
        glEnable(GL_DEPTH_TEST);
        markerVao_.release();
    }
    drawTriad();
    drawOverlay(mvp);
    drawLegend();
    drawViewCube();
}

void Viewport::setHighlightedRegions(std::uint64_t body, std::vector<std::int32_t> regions) {
    highlightBody_ = body;
    highlightRegions_ = std::move(regions);
    const std::vector<Body> same = bodies_;
    setBodies(same);  // rebuilds only the region colours that changed
    update();
}

void Viewport::setPickMarker(const Pick& p) {
    marker_ = p;
    update();
}

Pick Viewport::pickAt(const QPoint& pos) const {
    // The ray under the cursor, from the inverse of the camera's matrices.
    const QMatrix4x4 inv = (projMatrix() * viewMatrix()).inverted();
    const float x = 2.f * static_cast<float>(pos.x()) / static_cast<float>(std::max(1, width())) - 1.f;
    const float y = 1.f - 2.f * static_cast<float>(pos.y()) / static_cast<float>(std::max(1, height()));
    const QVector3D a = inv.map(QVector3D(x, y, -1.f)), b = inv.map(QVector3D(x, y, 1.f));
    const Vec3d o(a.x(), a.y(), a.z());
    Vec3d d(b.x() - a.x(), b.y() - a.y(), b.z() - a.z());
    d.normalize();
    Pick best;
    double bestT = 1e300;
    for (const auto& body : bodies_) {
        if (!body.visible || !body.mesh || body.mesh->faces.empty()) continue;
        const TriMesh& m = *body.mesh;
        // Slab test against the body's box first.
        const Box3 bx = m.bounds();
        double t0 = 0, t1 = 1e300;
        bool miss = false;
        for (int k = 0; k < 3 && !miss; ++k) {
            const double inv_d = 1.0 / (std::abs(d[k]) < 1e-300 ? 1e-300 : d[k]);
            double ta = (bx.min[k] - o[k]) * inv_d, tb = (bx.max[k] - o[k]) * inv_d;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            miss = t0 > t1;
        }
        if (miss || t0 > bestT) continue;
        // Möller–Trumbore against every face (a few ms per million faces).
        for (std::size_t f = 0; f < m.faces.size(); ++f) {
            const Tri& t = m.faces[f];
            const Vec3d p0 = m.vertices[t[0]].cast<double>();
            const Vec3d e1 = m.vertices[t[1]].cast<double>() - p0, e2 = m.vertices[t[2]].cast<double>() - p0;
            const Vec3d pv = d.cross(e2);
            const double det = e1.dot(pv);
            if (std::abs(det) < 1e-14) continue;
            const double id = 1.0 / det;
            const Vec3d tv = o - p0;
            const double u = tv.dot(pv) * id;
            if (u < 0 || u > 1) continue;
            const Vec3d qv = tv.cross(e1);
            const double v = d.dot(qv) * id;
            if (v < 0 || u + v > 1) continue;
            const double tt = e2.dot(qv) * id;
            if (tt <= 0 || tt >= bestT) continue;
            bestT = tt;
            best.body = body.id;
            best.face = static_cast<std::uint32_t>(f);
            best.point = o + d * tt;
            best.normal = e1.cross(e2).normalized();
        }
    }
    if (best.hit())
        for (const auto& body : bodies_)
            if (body.id == best.body && body.view && best.face < body.view->faceOf.size()) best.solidFace = static_cast<int>(body.view->faceOf[best.face]);
    // An edge of a part within a few pixels of the click, not hidden behind a surface.
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    const qreal sx = static_cast<qreal>(width()), sy = static_cast<qreal>(height());
    auto toScreen = [&](const Vec3f& p, double& depth) {
        const QVector4D c = mvp * QVector4D(p.x(), p.y(), p.z(), 1.f);
        depth = c.w();
        return QPointF((c.x() / c.w() * 0.5 + 0.5) * sx, (0.5 - c.y() / c.w() * 0.5) * sy);
    };
    const QPointF click(pos);
    // Over a face, an edge wins only right on it (3 px); in the empty space
    // beside a part, a little farther (6 px).
    double bestPx = best.hit() ? 3.0 : 6.0;
    for (const auto& body : bodies_) {
        if (!body.visible || !body.view) continue;
        for (std::size_t e = 0; e < body.view->edges.size(); ++e) {
            const auto& pts = body.view->edges[e];
            for (std::size_t k = 0; k + 1 < pts.size(); ++k) {
                double w0, w1;
                const QPointF sa = toScreen(pts[k], w0), sb = toScreen(pts[k + 1], w1);
                if (w0 <= 0 || w1 <= 0) continue;
                const QPointF ab = sb - sa;
                const double len2 = QPointF::dotProduct(ab, ab);
                const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(click - sa, ab) / len2, 0.0, 1.0) : 0.0;
                const QPointF q = sa + t * ab;
                const double px = std::hypot(click.x() - q.x(), click.y() - q.y());
                if (px >= bestPx) continue;
                const Vec3d w = (pts[k].cast<double>() * (1 - t) + pts[k + 1].cast<double>() * t);
                const double along = (w - o).dot(d);
                if (best.hit() && best.body != body.id && along > bestT + 1e-3 * sceneRadius_) continue;  // behind another body
                if (best.hit() && along > bestT + 0.005 * sceneRadius_) continue;  // behind a face of this one
                bestPx = px;
                best.solidEdge = static_cast<int>(e);
                if (!best.hit() || best.body != body.id) {
                    best.body = body.id;
                    best.solidFace = -1;
                    best.normal = -d;
                }
                best.point = w;
            }
        }
    }
    return best;
}

QImage Viewport::capture(int w, int h) {
    QImage img = grabFramebuffer();
    if (w > 0 && h > 0 && !img.isNull()) img = img.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

void Viewport::setGhosts(std::vector<Body> ghosts) {
    ghosts_ = std::move(ghosts);
    ghostsDirty_ = true;
    update();
}

void Viewport::setHandles(std::vector<Handle> handles) {
    handles_ = std::move(handles);
    if (dragHandle_ >= static_cast<int>(handles_.size())) dragHandle_ = -1;
    update();
}

void Viewport::setGuides(std::vector<Guide> guides) {
    guides_ = std::move(guides);
    update();
}

bool Viewport::screenPoint(const Vec3d& v, QPointF& out) const {
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    const QVector4D c = mvp * QVector4D(static_cast<float>(v.x()), static_cast<float>(v.y()), static_cast<float>(v.z()), 1.f);
    if (c.w() <= 0) return false;
    out = QPointF((c.x() / c.w() * 0.5 + 0.5) * static_cast<qreal>(width()), (0.5 - c.y() / c.w() * 0.5) * static_cast<qreal>(height()));
    return true;
}

void Viewport::setLassoMode(bool on) {
    lassoMode_ = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void Viewport::setSelection(std::uint64_t body, std::vector<std::uint32_t> faces) {
    selBody_ = faces.empty() ? 0 : body;
    selFaces_ = std::move(faces);
    selMesh_ = nullptr;
    for (const auto& b : bodies_)
        if (b.id == selBody_) selMesh_ = b.mesh.get();
    selDirty_ = true;
    update();
}

void Viewport::setPairMarks(std::vector<Vec3d> moving, std::vector<Vec3d> reference) {
    pairMoving_ = std::move(moving);
    pairReference_ = std::move(reference);
    update();
}

void Viewport::lassoSelect(bool add) {
    // The scan to select on: the selected body if it is a scan, else the first visible scan.
    const Body* target = nullptr;
    for (const auto& b : bodies_)
        if (b.id == selected_ && b.visible && !b.isPart()) target = &b;
    for (const auto& b : bodies_)
        if (!target && b.visible && !b.isPart()) target = &b;
    if (!target) return;
    const TriMesh& m = *target->mesh;
    const QPolygonF poly(lassoPath_);
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    const QVector3D e3 = eye();
    const Vec3d eyeP(e3.x(), e3.y(), e3.z());
    const qreal sx = static_cast<qreal>(width()), sy = static_cast<qreal>(height());
    auto inside = [&](const Vec3d& p) {
        const QVector4D c = mvp * QVector4D(static_cast<float>(p.x()), static_cast<float>(p.y()), static_cast<float>(p.z()), 1.f);
        if (c.w() <= 0) return false;
        const QPointF q((c.x() / c.w() * 0.5 + 0.5) * sx, (0.5 - c.y() / c.w() * 0.5) * sy);
        return poly.containsPoint(q, Qt::OddEvenFill);
    };
    std::vector<std::uint32_t> out;
    if (m.isPointCloud()) {
        const bool hn = m.hasNormals();
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            const Vec3d p = m.vertices[i].cast<double>();
            if (hn && m.normals[i].cast<double>().dot(eyeP - p) < 0) continue;
            if (inside(p)) out.push_back(static_cast<std::uint32_t>(i));
        }
    } else {
        for (std::size_t f = 0; f < m.faces.size(); ++f) {
            const Tri& t = m.faces[f];
            const Vec3d a = m.vertices[t[0]].cast<double>(), b = m.vertices[t[1]].cast<double>(), c = m.vertices[t[2]].cast<double>();
            const Vec3d ctr = (a + b + c) / 3.0;
            if ((b - a).cross(c - a).dot(eyeP - ctr) <= 0) continue;  // facing away: not seen
            if (inside(ctr)) out.push_back(static_cast<std::uint32_t>(f));
        }
    }
    emit lassoSelected(target->id, std::move(out), add);
}

void Viewport::drawOverlay(const QMatrix4x4& mvp) {
    // The selection, in red over the scan.
    const Body* sb = nullptr;
    for (const auto& b : bodies_)
        if (b.id == selBody_ && b.visible) sb = &b;
    if (sb && selDirty_) {
        selDirty_ = false;
        const TriMesh& m = *sb->mesh;
        std::vector<float> buf;
        selPoints_ = m.isPointCloud();
        auto put = [&](const Vec3f& v) { buf.insert(buf.end(), {v.x(), v.y(), v.z(), 0.95f, 0.15f, 0.12f}); };
        for (auto f : selFaces_) {
            if (selPoints_) {
                if (f < m.vertices.size()) put(m.vertices[f]);
            } else if (f < m.faces.size()) {
                for (auto i : m.faces[f]) put(m.vertices[i]);
            }
        }
        if (!selVao_.isCreated()) selVao_.create();
        selVao_.bind();
        if (!selVbo_.isCreated()) selVbo_.create();
        selVbo_.bind();
        selVbo_.allocate(buf.data(), static_cast<int>(buf.size() * sizeof(float)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
        selVao_.release();
        selCount_ = static_cast<int>(buf.size() / 6);
    }
    if (sb && selCount_ > 0) {
        lineProg_.bind();
        lineProg_.setUniformValue("uMvp", mvp);
        lineProg_.setUniformValue("uTint", QVector4D(0, 0, 0, 0));
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.f, -1.f);
        selVao_.bind();
        if (selPoints_) {
            glPointSize(4.f);
            glDrawArrays(GL_POINTS, 0, selCount_);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, selCount_);
        }
        selVao_.release();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthFunc(GL_LESS);
    }
    // Pair marks and the lasso being drawn: on top, in screen space.
    if (lassoPath_.isEmpty() && pairMoving_.empty() && pairReference_.empty() && guides_.empty() && handles_.empty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal sx = static_cast<qreal>(width()), sy = static_cast<qreal>(height());
    auto screen = [&](const Vec3d& v, bool& ok) {
        const QVector4D c = mvp * QVector4D(static_cast<float>(v.x()), static_cast<float>(v.y()), static_cast<float>(v.z()), 1.f);
        ok = c.w() > 0;
        return QPointF((c.x() / c.w() * 0.5 + 0.5) * sx, (0.5 - c.y() / c.w() * 0.5) * sy);
    };
    auto marks = [&](const std::vector<Vec3d>& pts, const QColor& col) {
        for (std::size_t i = 0; i < pts.size(); ++i) {
            bool ok = false;
            const QPointF q = screen(pts[i], ok);
            if (!ok) continue;
            p.setPen(QPen(Qt::black, 1.5));
            p.setBrush(col);
            p.drawEllipse(q, 9, 9);
            p.setPen(Qt::black);
            p.drawText(QRectF(q.x() - 9, q.y() - 9, 18, 18), Qt::AlignCenter, QString::number(i + 1));
        }
    };
    for (const auto& g : guides_) {
        QPointF qa, qb;
        if (!screenPoint(g.a, qa) || !screenPoint(g.b, qb)) continue;
        p.setPen(QPen(g.color, 1.5));
        p.drawLine(qa, qb);
    }
    for (std::size_t i = 0; i < handles_.size(); ++i) {
        QPointF q;
        if (!screenPoint(handles_[i].pos, q)) continue;
        const bool active = static_cast<int>(i) == dragHandle_;
        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(handles_[i].color);
        p.drawEllipse(q, active ? 8 : 6, active ? 8 : 6);
    }
    marks(pairMoving_, QColor(255, 150, 40));
    marks(pairReference_, QColor(60, 200, 230));
    if (lassoPath_.size() >= 2) {
        p.setPen(QPen(QColor(230, 40, 30), 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF(lassoPath_));
    }
}

void Viewport::mousePressEvent(QMouseEvent* e) {
    // The view cube: front faces first (they are drawn last).
    if (e->button() == Qt::LeftButton && cubeArea_.contains(e->position())) {
        for (auto it = cubeFaces_.rbegin(); it != cubeFaces_.rend(); ++it)
            if (it->first.containsPoint(e->position(), Qt::OddEvenFill)) {
                setView(it->second);
                return;
            }
        setView(View::Iso);
        return;
    }
    lastPos_ = e->position().toPoint();
    dragButton_ = e->button();
    dragged_ = false;
    // A handle under the pointer is dragged, not the view.
    dragHandle_ = -1;
    if (e->button() == Qt::LeftButton) {
        double best = 10.0;
        for (std::size_t i = 0; i < handles_.size(); ++i) {
            QPointF q;
            if (!screenPoint(handles_[i].pos, q)) continue;
            const double d = std::hypot(q.x() - e->position().x(), q.y() - e->position().y());
            if (d < best) {
                best = d;
                dragHandle_ = static_cast<int>(i);
            }
        }
        if (dragHandle_ >= 0) {
            const QVector3D ey = eye(), c = target_;
            dragNormal_ = Vec3d(ey.x() - c.x(), ey.y() - c.y(), ey.z() - c.z()).normalized();
            return;
        }
    }
    lassoing_ = lassoMode_ && e->button() == Qt::LeftButton && !(e->modifiers() & Qt::AltModifier);
    if (lassoing_) {
        lassoPath_.clear();
        lassoPath_.push_back(e->position());
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
    const QPoint p = e->position().toPoint();
    const QPoint d = p - lastPos_;
    lastPos_ = p;
    if (d.manhattanLength() > 0) dragged_ = true;
    if (dragHandle_ >= 0) {
        // Along the screen, at the handle's depth: the ray meets the plane
        // through the handle facing the camera.
        const QMatrix4x4 inv = (projMatrix() * viewMatrix()).inverted();
        const float x = 2.f * static_cast<float>(e->position().x()) / static_cast<float>(std::max(1, width())) - 1.f;
        const float y = 1.f - 2.f * static_cast<float>(e->position().y()) / static_cast<float>(std::max(1, height()));
        const QVector3D a = inv.map(QVector3D(x, y, -1.f)), b = inv.map(QVector3D(x, y, 1.f));
        const Vec3d o(a.x(), a.y(), a.z()), dir = Vec3d(b.x() - a.x(), b.y() - a.y(), b.z() - a.z()).normalized();
        Handle& h = handles_[static_cast<std::size_t>(dragHandle_)];
        const double den = dir.dot(dragNormal_);
        if (std::abs(den) > 1e-9) {
            h.pos = o + dir * ((h.pos - o).dot(dragNormal_) / den);
            emit handleDragged(h.id, h.pos, false);
        }
        lastPos_ = e->position().toPoint();
        update();
        return;
    }
    if (lassoing_) {
        lassoPath_.push_back(e->position());
        update();
        return;
    }
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
    if (dragHandle_ >= 0) {
        const Handle h = handles_[static_cast<std::size_t>(dragHandle_)];
        dragHandle_ = -1;
        dragButton_ = Qt::NoButton;
        if (dragged_) emit handleDragged(h.id, h.pos, true);
        return;
    }
    if (lassoing_) {
        lassoing_ = false;
        dragButton_ = Qt::NoButton;
        if (dragged_ && lassoPath_.size() >= 3) {
            lassoSelect((e->modifiers() & Qt::ControlModifier) != 0);
            lassoPath_.clear();
            update();
            return;
        }
        lassoPath_.clear();
    }
    if (!dragged_ && e->button() == Qt::LeftButton) {
        const Pick p = pickAt(e->position().toPoint());
        emit picked(p, (e->modifiers() & Qt::ControlModifier) != 0);
        emit bodyClicked(p.body);
    } else if (!dragged_ && e->button() == Qt::RightButton) {
        emit contextRequested(pickAt(e->position().toPoint()), e->globalPosition().toPoint());
    }
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

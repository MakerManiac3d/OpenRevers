// SPDX-License-Identifier: GPL-3.0-or-later
#include "doc/Document.h"

#include <algorithm>
#include <stdexcept>

namespace orv {

namespace {
constexpr std::size_t kMaxUndo = 100;
}

Document::Document() = default;

std::vector<Body> Document::bodies() const {
    std::lock_guard lock(m_);
    std::vector<Body> out = state_.bodies;
    for (auto& b : out) withDisplay(b);
    return out;
}

void Document::withDisplay(Body& b) const {
    // A deviation map shows only on the triangles it was measured on.
    auto it = deviation_.find(b.id);
    b.deviation = it != deviation_.end() && it->second->mesh == b.mesh.get() ? it->second : nullptr;
}

std::optional<Body> Document::findBody(const std::string& ref) const {
    std::lock_guard lock(m_);
    for (const auto& b : state_.bodies)
        if (b.ref() == ref || b.name == ref) {
            Body out = b;
            withDisplay(out);
            return out;
        }
    return std::nullopt;
}

Body Document::body(const std::string& ref) const {
    auto b = findBody(ref);
    if (!b) {
        std::string known;
        for (const auto& x : bodies()) known += (known.empty() ? "" : ", ") + x.ref() + " (" + x.name + ")";
        throw std::invalid_argument("No body \"" + ref + "\". Bodies: " + (known.empty() ? "none" : known));
    }
    return *b;
}

Body* Document::find(std::uint64_t id) {
    for (auto& b : state_.bodies)
        if (b.id == id) return &b;
    throw std::invalid_argument("No body b" + std::to_string(id));
}

void Document::beginChange(const std::string& label) {
    std::lock_guard lock(m_);
    if (depth_++ == 0) pending_ = Step{label, state_};
}

void Document::endChange() {
    bool changed = false;
    {
        std::lock_guard lock(m_);
        if (depth_ == 0) return;
        if (--depth_ > 0) return;
        undo_.push_back(std::move(*pending_));
        pending_.reset();
        if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
        redo_.clear();
        state_.version = ++versionCounter_;
        changed = true;
    }
    if (changed) notify();
}

void Document::abortChange() {
    {
        std::lock_guard lock(m_);
        if (depth_ == 0) return;
        depth_ = 0;
        if (pending_) state_ = std::move(pending_->before);
        pending_.reset();
    }
    notify();
}

Body Document::addBody(std::string name, std::shared_ptr<const TriMesh> mesh, std::string source) {
    std::lock_guard lock(m_);
    Body b;
    b.id = state_.nextId++;
    // Unique name: "part", "part 2", "part 3"…
    std::string base = name.empty() ? "Body" : name;
    std::string candidate = base;
    for (int i = 2; std::any_of(state_.bodies.begin(), state_.bodies.end(), [&](const Body& x) { return x.name == candidate; }); ++i)
        candidate = base + " " + std::to_string(i);
    b.name = candidate;
    b.mesh = std::move(mesh);
    b.source = std::move(source);
    static const std::array<std::array<float, 3>, 5> palette{{{0.62f, 0.70f, 0.78f}, {0.80f, 0.66f, 0.52f}, {0.60f, 0.76f, 0.60f}, {0.76f, 0.62f, 0.78f}, {0.82f, 0.78f, 0.56f}}};
    b.color = palette[(b.id - 1) % palette.size()];
    state_.bodies.push_back(b);
    return b;
}

void Document::setMesh(std::uint64_t id, std::shared_ptr<const TriMesh> mesh, std::shared_ptr<const Features> features) {
    std::lock_guard lock(m_);
    Body* b = find(id);
    b->mesh = std::move(mesh);
    b->features = std::move(features);
}

PartTriangles partTriangles(const Solid& solid) {
    PartTriangles out;
    if (solid.empty()) {
        out.mesh = std::make_shared<TriMesh>();
        out.view = std::make_shared<PartView>();
        return out;
    }
    // Fine enough that a Ø2.75 hole looks round and a 0.2 mm step shows.
    const auto b = solid.bounds();
    const double deflection = std::clamp(0.0005 * (b.max() - b.min()).norm(), 0.002, 0.05);
    SolidTessellation t = solid.tessellate(deflection);
    auto view = std::make_shared<PartView>();
    view->faceOf = std::move(t.faceOf);
    view->edges = std::move(t.edges);
    view->edgeMiddle = std::move(t.edgeMiddle);
    out.mesh = std::make_shared<TriMesh>(std::move(t.mesh));
    out.view = std::move(view);
    return out;
}

Body Document::addPart(std::string name, std::shared_ptr<const Model> model, Solid solid, PartTriangles triangles, std::string buildError) {
    std::lock_guard lock(m_);
    Body b = addBody(std::move(name), std::move(triangles.mesh));
    Body* nb = find(b.id);
    nb->model = std::move(model);
    nb->solid = std::move(solid);
    nb->view = std::move(triangles.view);
    nb->buildError = std::move(buildError);
    nb->color = {0.78f, 0.80f, 0.84f};
    return *nb;
}

void Document::setPart(std::uint64_t id, std::shared_ptr<const Model> model, Solid solid, PartTriangles triangles, std::string buildError) {
    std::lock_guard lock(m_);
    Body* b = find(id);
    b->model = std::move(model);
    b->solid = std::move(solid);
    b->mesh = std::move(triangles.mesh);
    b->view = std::move(triangles.view);
    b->features = nullptr;
    b->buildError = std::move(buildError);
}

void Document::setOpacity(std::uint64_t id, float opacity) {
    std::lock_guard lock(m_);
    find(id)->opacity = std::clamp(opacity, 0.05f, 1.0f);
}

void Document::setDeviation(std::uint64_t id, std::shared_ptr<const DeviationMap> map) {
    {
        std::lock_guard lock(m_);
        find(id);
        if (map) deviation_[id] = std::move(map);
        else deviation_.erase(id);
    }
    notify();  // shown at once; not a step of the history, nor saved
}

void Document::setFeatures(std::uint64_t id, std::shared_ptr<const Features> features) {
    std::lock_guard lock(m_);
    find(id)->features = std::move(features);
}

void Document::renameBody(std::uint64_t id, const std::string& name) {
    std::lock_guard lock(m_);
    if (name.empty()) throw std::invalid_argument("A body needs a name");
    for (const auto& b : state_.bodies)
        if (b.id != id && b.name == name) throw std::invalid_argument("Another body is already called \"" + name + "\"");
    find(id)->name = name;
}

void Document::setVisible(std::uint64_t id, bool visible) {
    std::lock_guard lock(m_);
    find(id)->visible = visible;
}

void Document::setColor(std::uint64_t id, std::array<float, 3> color) {
    std::lock_guard lock(m_);
    find(id)->color = color;
}

void Document::removeBody(std::uint64_t id) {
    std::lock_guard lock(m_);
    find(id);
    std::erase_if(state_.bodies, [&](const Body& b) { return b.id == id; });
}

void Document::reset(std::vector<Body> bodies, std::uint64_t nextId, std::string path) {
    {
        std::lock_guard lock(m_);
        state_.bodies = std::move(bodies);
        state_.nextId = std::max<std::uint64_t>(nextId, 1);
        for (const auto& b : state_.bodies) state_.nextId = std::max(state_.nextId, b.id + 1);
        undo_.clear();
        redo_.clear();
        pending_.reset();
        depth_ = 0;
        path_ = std::move(path);
        state_.version = ++versionCounter_;
        savedVersion_ = state_.version;
    }
    notify();
}

bool Document::canUndo() const { std::lock_guard lock(m_); return !undo_.empty(); }
bool Document::canRedo() const { std::lock_guard lock(m_); return !redo_.empty(); }

std::string Document::undo() {
    std::string label;
    {
        std::lock_guard lock(m_);
        if (undo_.empty() || depth_) return {};
        Step s = std::move(undo_.back());
        undo_.pop_back();
        label = s.label;
        redo_.push_back(Step{s.label, state_});
        state_ = std::move(s.before);
    }
    notify();
    return label;
}

std::string Document::redo() {
    std::string label;
    {
        std::lock_guard lock(m_);
        if (redo_.empty() || depth_) return {};
        Step s = std::move(redo_.back());
        redo_.pop_back();
        label = s.label;
        undo_.push_back(Step{s.label, state_});
        state_ = std::move(s.before);
    }
    notify();
    return label;
}

std::vector<HistoryEntry> Document::undoList() const {
    std::lock_guard lock(m_);
    std::vector<HistoryEntry> out;
    for (const auto& s : undo_) out.push_back({s.label});
    return out;
}

std::vector<HistoryEntry> Document::redoList() const {
    std::lock_guard lock(m_);
    std::vector<HistoryEntry> out;
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) out.push_back({it->label});
    return out;
}

std::string Document::path() const { std::lock_guard lock(m_); return path_; }
void Document::setPath(const std::string& p) { std::lock_guard lock(m_); path_ = p; }
bool Document::modified() const { std::lock_guard lock(m_); return state_.version != savedVersion_; }
void Document::setSaved() { { std::lock_guard lock(m_); savedVersion_ = state_.version; } notify(); }
std::uint64_t Document::nextId() const { std::lock_guard lock(m_); return state_.nextId; }

void Document::setOnChanged(std::function<void()> callback) {
    std::lock_guard lock(m_);
    onChanged_ = std::move(callback);
}

void Document::notify() {
    std::function<void()> cb;
    {
        std::lock_guard lock(m_);
        cb = onChanged_;
    }
    if (cb) cb();
}

}  // namespace orv

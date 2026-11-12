// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the open project: its bodies and the history of what was done
// to them, with undo and redo. Thread-safe: commands may run on a worker
// thread while the interface reads.
#pragma once

#include "core/mesh/TriMesh.h"
#include "doc/Features.h"
#include "doc/Model.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace orv {

/// What the view needs of a part beyond its triangles: the solid face each
/// triangle comes from, and the solid's edges as polylines.
struct PartView {
    std::vector<std::uint32_t> faceOf;
    std::vector<std::vector<Vec3f>> edges;
    std::vector<Vec3d> edgeMiddle;
};

/// A part's triangles for drawing and measuring, and its view.
struct PartTriangles {
    std::shared_ptr<const TriMesh> mesh;
    std::shared_ptr<const PartView> view;
};
PartTriangles partTriangles(const Solid& solid);

/// How far each vertex of a scan is from a part (signed: positive outside
/// it), from part.compare, to colour the scan by it.
struct DeviationMap {
    std::string part;           // the part's ref it was measured against
    const TriMesh* mesh{nullptr};  // the scan's triangles it was measured on
    std::vector<float> distance;  // per vertex
    float tolerance{0.2f};      // green within ±tolerance
    float range{1.0f};          // full red or blue at ±range
};

/// A body of the project. Its mesh is shared and never changed in place: a
/// change replaces the pointer, so an undo step costs no copy of the triangles.
struct Body {
    std::uint64_t id{0};
    std::string name;
    std::shared_ptr<const TriMesh> mesh;
    std::shared_ptr<const Features> features;  // shapes found on it (null: none)
    // A parametric part: its history, the exact solid it builds and, when an
    // operation failed, why. Its mesh is then only the solid's triangles,
    // for drawing and measuring.
    std::shared_ptr<const Model> model;
    Solid solid;
    std::shared_ptr<const PartView> view;
    std::string buildError;
    // A scan coloured by its distance to a part (null: its own colour). Not
    // part of the history nor of the project: filled in when the body is read.
    std::shared_ptr<const DeviationMap> deviation;
    bool visible{true};
    float opacity{1.0f};  // below 1: seen through (a scan over its part, or the reverse)
    std::array<float, 3> color{0.62f, 0.70f, 0.78f};
    std::string source;  // file it came from, if any

    std::string ref() const { return "b" + std::to_string(id); }
    bool isPart() const { return model != nullptr; }
};

struct HistoryEntry {
    std::string label;
};

class Document {
public:
    Document();

    /// Copies of the bodies, in order (cheap: meshes are shared).
    std::vector<Body> bodies() const;
    /// A body by reference: its "b<id>" or its exact name. Throws std::invalid_argument.
    Body body(const std::string& ref) const;
    std::optional<Body> findBody(const std::string& ref) const;

    // Changes. Each call between beginChange and endChange is one undo step.
    void beginChange(const std::string& label);
    void endChange();
    /// Undo what was changed since beginChange (a command that failed).
    void abortChange();

    Body addBody(std::string name, std::shared_ptr<const TriMesh> mesh, std::string source = {});
    /// Replace the mesh. Its shapes no longer match and are dropped, unless
    /// `features` gives the ones that go with the new mesh (a move keeps them).
    void setMesh(std::uint64_t id, std::shared_ptr<const TriMesh> mesh, std::shared_ptr<const Features> features = nullptr);
    void setFeatures(std::uint64_t id, std::shared_ptr<const Features> features);
    /// A new parametric part, or a part's new state after an edit.
    Body addPart(std::string name, std::shared_ptr<const Model> model, Solid solid, PartTriangles triangles, std::string buildError = {});
    void setPart(std::uint64_t id, std::shared_ptr<const Model> model, Solid solid, PartTriangles triangles, std::string buildError = {});
    void setOpacity(std::uint64_t id, float opacity);
    /// Colour a body by its distance to a part (null: its own colour again).
    /// Shown at once; not a step of the history.
    void setDeviation(std::uint64_t id, std::shared_ptr<const DeviationMap> map);
    void renameBody(std::uint64_t id, const std::string& name);
    void setVisible(std::uint64_t id, bool visible);
    void setColor(std::uint64_t id, std::array<float, 3> color);
    void removeBody(std::uint64_t id);
    /// Replace everything (opening a project or starting a new one); clears the history.
    void reset(std::vector<Body> bodies, std::uint64_t nextId, std::string path = {});

    bool canUndo() const;
    bool canRedo() const;
    std::string undo();  // returns the label undone, empty if nothing
    std::string redo();
    std::vector<HistoryEntry> undoList() const;
    std::vector<HistoryEntry> redoList() const;

    std::string path() const;
    void setPath(const std::string& p);
    bool modified() const;
    void setSaved();
    std::uint64_t nextId() const;

    /// Called (from whatever thread changed the document) after each change.
    void setOnChanged(std::function<void()> callback);

private:
    struct State {
        std::vector<Body> bodies;
        std::uint64_t nextId{1};
        std::uint64_t version{0};  // identifies this state, for "modified since saved"
    };
    struct Step {
        std::string label;
        State before;
    };
    void notify();
    Body* find(std::uint64_t id);
    void withDisplay(Body& b) const;

    mutable std::recursive_mutex m_;
    State state_;
    std::vector<Step> undo_, redo_;
    std::optional<Step> pending_;
    int depth_{0};
    std::string path_;
    std::uint64_t savedVersion_{0};
    std::uint64_t versionCounter_{0};
    std::function<void()> onChanged_;
    std::map<std::uint64_t, std::shared_ptr<const DeviationMap>> deviation_;
};

}  // namespace orv

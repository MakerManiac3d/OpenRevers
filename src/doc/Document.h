// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the open project: its bodies and the history of what was done
// to them, with undo and redo. Thread-safe: commands may run on a worker
// thread while the interface reads.
#pragma once

#include "core/mesh/TriMesh.h"

#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace orv {

/// A body of the project. Its mesh is shared and never changed in place: a
/// change replaces the pointer, so an undo step costs no copy of the triangles.
struct Body {
    std::uint64_t id{0};
    std::string name;
    std::shared_ptr<const TriMesh> mesh;
    bool visible{true};
    std::array<float, 3> color{0.62f, 0.70f, 0.78f};
    std::string source;  // file it came from, if any

    std::string ref() const { return "b" + std::to_string(id); }
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
    void setMesh(std::uint64_t id, std::shared_ptr<const TriMesh> mesh);
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

    mutable std::recursive_mutex m_;
    State state_;
    std::vector<Step> undo_, redo_;
    std::optional<Step> pending_;
    int depth_{0};
    std::string path_;
    std::uint64_t savedVersion_{0};
    std::uint64_t versionCounter_{0};
    std::function<void()> onChanged_;
};

}  // namespace orv

// SPDX-License-Identifier: GPL-3.0-or-later
#include "doc/Project.h"

#include "core/mesh/MeshIO.h"
#include "core/util/Version.h"

#include <nlohmann/json.hpp>

#include <bit>
#include <cstring>
#include <fstream>

namespace orv {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
constexpr char kMagic[8] = {'O', 'R', 'V', 'P', 'R', 'O', 'J', '\0'};
constexpr std::uint32_t kVersion = 1;
static_assert(std::endian::native == std::endian::little, "project files are little endian");
}  // namespace

void saveProject(const Document& doc, const fs::path& path) {
    const auto bodies = doc.bodies();
    json manifest;
    manifest["application"] = "OpenRevers";
    manifest["applicationVersion"] = kVersionString;
    manifest["units"] = "mm";
    manifest["nextId"] = doc.nextId();
    std::uint64_t offset = 0;
    for (const auto& b : bodies) {
        json jb;
        jb["id"] = b.id;
        jb["name"] = b.name;
        jb["visible"] = b.visible;
        jb["color"] = b.color;
        jb["source"] = b.source;
        jb["kind"] = "mesh";
        jb["vertices"] = b.mesh->vertices.size();
        jb["faces"] = b.mesh->faces.size();
        jb["offset"] = offset;
        offset += b.mesh->vertices.size() * 12 + b.mesh->faces.size() * 12;
        manifest["bodies"].push_back(jb);
    }
    if (!manifest.contains("bodies")) manifest["bodies"] = json::array();
    const std::string text = manifest.dump(2);

    // Write to a temporary file and rename, so a failed save never destroys the old project.
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) throw IoError("Cannot write " + path.string());
        out.write(kMagic, 8);
        out.write(reinterpret_cast<const char*>(&kVersion), 4);
        const std::uint64_t len = text.size();
        out.write(reinterpret_cast<const char*>(&len), 8);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        for (const auto& b : bodies) {
            out.write(reinterpret_cast<const char*>(b.mesh->vertices.data()), static_cast<std::streamsize>(b.mesh->vertices.size() * 12));
            out.write(reinterpret_cast<const char*>(b.mesh->faces.data()), static_cast<std::streamsize>(b.mesh->faces.size() * 12));
        }
        if (!out) throw IoError("Failed while writing " + path.string());
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) throw IoError("Cannot replace " + path.string() + ": " + ec.message());
    }
}

void loadProject(Document& doc, const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw IoError("Cannot open " + path.string());
    char magic[8];
    std::uint32_t version = 0;
    std::uint64_t len = 0;
    in.read(magic, 8);
    in.read(reinterpret_cast<char*>(&version), 4);
    in.read(reinterpret_cast<char*>(&len), 8);
    if (!in || std::memcmp(magic, kMagic, 8) != 0) throw IoError(path.filename().string() + " is not an OpenRevers project");
    if (version > kVersion) throw IoError(path.filename().string() + " was saved by a newer OpenRevers (format " + std::to_string(version) + ")");
    if (len > (1ull << 30)) throw IoError(path.filename().string() + ": manifest too large");
    std::string text(len, '\0');
    in.read(text.data(), static_cast<std::streamsize>(len));
    if (!in) throw IoError(path.filename().string() + ": truncated manifest");
    json manifest;
    try {
        manifest = json::parse(text);
    } catch (const json::exception& e) {
        throw IoError(path.filename().string() + ": damaged manifest (" + e.what() + ")");
    }
    const auto dataStart = static_cast<std::uint64_t>(in.tellg());
    in.seekg(0, std::ios::end);
    const auto fileSize = static_cast<std::uint64_t>(in.tellg());

    std::vector<Body> bodies;
    for (const auto& jb : manifest.value("bodies", json::array())) {
        Body b;
        b.id = jb.at("id").get<std::uint64_t>();
        b.name = jb.at("name").get<std::string>();
        b.visible = jb.value("visible", true);
        if (jb.contains("color")) b.color = jb["color"].get<std::array<float, 3>>();
        b.source = jb.value("source", "");
        const auto nv = jb.at("vertices").get<std::uint64_t>();
        const auto nf = jb.at("faces").get<std::uint64_t>();
        const auto off = jb.at("offset").get<std::uint64_t>();
        if (dataStart + off + nv * 12 + nf * 12 > fileSize) throw IoError(path.filename().string() + ": body \"" + b.name + "\" is truncated");
        auto mesh = std::make_shared<TriMesh>();
        mesh->vertices.resize(nv);
        mesh->faces.resize(nf);
        in.seekg(static_cast<std::streamoff>(dataStart + off));
        in.read(reinterpret_cast<char*>(mesh->vertices.data()), static_cast<std::streamsize>(nv * 12));
        in.read(reinterpret_cast<char*>(mesh->faces.data()), static_cast<std::streamsize>(nf * 12));
        for (const auto& t : mesh->faces)
            for (auto v : t)
                if (v >= nv) throw IoError(path.filename().string() + ": body \"" + b.name + "\" refers to a vertex that does not exist");
        b.mesh = std::move(mesh);
        bodies.push_back(std::move(b));
    }
    doc.reset(std::move(bodies), manifest.value("nextId", std::uint64_t{1}), path.string());
}

}  // namespace orv

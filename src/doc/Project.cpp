// SPDX-License-Identifier: GPL-3.0-or-later
#include "doc/Project.h"

#include "core/mesh/MeshIO.h"
#include "core/util/Version.h"

#include <nlohmann/json.hpp>

#include <bit>
#include <cstring>
#include <fstream>

namespace orv {

namespace {

using nlohmann::json;

json vec(const Vec3d& v) { return json::array({v.x(), v.y(), v.z()}); }
Vec3d vec(const json& j) { return {j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()}; }

json featuresToJson(const Features& f) {
    json list = json::array();
    for (const auto& x : f.list)
        list.push_back({{"id", x.id}, {"name", x.name}, {"type", primitiveName(x.shape.type)}, {"origin", vec(x.shape.origin)},
                        {"axis", vec(x.shape.axis)}, {"radius", x.shape.radius}, {"radius2", x.shape.radius2}, {"angle", x.shape.angle},
                        {"rms", x.shape.rms}, {"maxAbs", x.shape.maxAbs}, {"points", x.shape.points}, {"outliers", x.shape.outliers},
                        {"region", x.region}, {"faces", x.faces}, {"area", x.area}});
    return {{"noise", f.noise}, {"nextId", f.nextId}, {"list", list}};
}

Features featuresFromJson(const json& j) {
    Features f;
    f.noise = j.value("noise", 0.0);
    f.nextId = j.value("nextId", 1u);
    for (const auto& x : j.at("list")) {
        Feature e;
        e.id = x.at("id").get<std::uint32_t>();
        e.name = x.value("name", std::string{});
        const auto t = primitiveFromName(x.at("type").get<std::string>());
        if (!t) throw IoError("unknown shape type " + x.at("type").get<std::string>());
        e.shape.type = *t;
        e.shape.origin = vec(x.at("origin"));
        e.shape.axis = vec(x.at("axis"));
        e.shape.radius = x.value("radius", 0.0);
        e.shape.radius2 = x.value("radius2", 0.0);
        e.shape.angle = x.value("angle", 0.0);
        e.shape.rms = x.value("rms", 0.0);
        e.shape.maxAbs = x.value("maxAbs", 0.0);
        e.shape.points = x.value("points", std::size_t{0});
        e.shape.outliers = x.value("outliers", std::size_t{0});
        e.region = x.value("region", -1);
        e.faces = x.value("faces", std::size_t{0});
        e.area = x.value("area", 0.0);
        f.list.push_back(std::move(e));
        f.nextId = std::max(f.nextId, f.list.back().id + 1);
    }
    return f;
}

}  // namespace
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
constexpr char kMagic[8] = {'O', 'R', 'V', 'P', 'R', 'O', 'J', '\0'};
// 1: vertices and triangles. 2: adds the quads of a quad mesh ("quads", may be 0).
// 3: adds the normals of a point cloud ("normals": 0 or one per vertex).
// 4: adds the shapes found on a body ("features" in the manifest, and
//    "faceRegions": 0 or one int32 region per face in the data).
constexpr std::uint32_t kVersion = 4;
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
        jb["quads"] = b.mesh->quads.size();
        const std::size_t nn = b.mesh->hasNormals() ? b.mesh->normals.size() : 0;
        jb["normals"] = nn;
        if (b.mesh->isPointCloud()) jb["kind"] = "points";
        const std::size_t nr = b.features ? b.features->faceRegion.size() : 0;
        jb["faceRegions"] = nr;
        if (b.features) jb["features"] = featuresToJson(*b.features);
        jb["offset"] = offset;
        offset += b.mesh->vertices.size() * 12 + b.mesh->faces.size() * 12 + b.mesh->quads.size() * 16 + nn * 12 + nr * 4;
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
            out.write(reinterpret_cast<const char*>(b.mesh->quads.data()), static_cast<std::streamsize>(b.mesh->quads.size() * 16));
            if (b.mesh->hasNormals())
                out.write(reinterpret_cast<const char*>(b.mesh->normals.data()), static_cast<std::streamsize>(b.mesh->normals.size() * 12));
            if (b.features && !b.features->faceRegion.empty())
                out.write(reinterpret_cast<const char*>(b.features->faceRegion.data()), static_cast<std::streamsize>(b.features->faceRegion.size() * 4));
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
        const auto nq = jb.value("quads", std::uint64_t{0});
        const auto nn = jb.value("normals", std::uint64_t{0});
        const auto nr = jb.value("faceRegions", std::uint64_t{0});
        const auto off = jb.at("offset").get<std::uint64_t>();
        if (nr != 0 && nr != nf) throw IoError(path.filename().string() + ": body \"" + b.name + "\" has a wrong number of face regions");
        if (nn != 0 && nn != nv) throw IoError(path.filename().string() + ": body \"" + b.name + "\" has a wrong number of normals");
        if (nv > fileSize || nf > fileSize || nq > fileSize || off > fileSize ||
            dataStart + off + nv * 12 + nf * 12 + nq * 16 + nn * 12 + nr * 4 > fileSize) throw IoError(path.filename().string() + ": body \"" + b.name + "\" is truncated");
        auto mesh = std::make_shared<TriMesh>();
        mesh->vertices.resize(nv);
        mesh->faces.resize(nf);
        in.seekg(static_cast<std::streamoff>(dataStart + off));
        in.read(reinterpret_cast<char*>(mesh->vertices.data()), static_cast<std::streamsize>(nv * 12));
        in.read(reinterpret_cast<char*>(mesh->faces.data()), static_cast<std::streamsize>(nf * 12));
        mesh->quads.resize(nq);
        in.read(reinterpret_cast<char*>(mesh->quads.data()), static_cast<std::streamsize>(nq * 16));
        mesh->normals.resize(nn);
        in.read(reinterpret_cast<char*>(mesh->normals.data()), static_cast<std::streamsize>(nn * 12));
        if (jb.contains("features")) {
            auto feats = std::make_shared<Features>(featuresFromJson(jb["features"]));
            feats->faceRegion.resize(nr);
            in.read(reinterpret_cast<char*>(feats->faceRegion.data()), static_cast<std::streamsize>(nr * 4));
            for (auto r : feats->faceRegion)
                if (r < -1) throw IoError(path.filename().string() + ": body \"" + b.name + "\" has a bad face region");
            b.features = std::move(feats);
        }
        bool bad = false;
        for (const auto& t : mesh->faces)
            for (auto v : t) bad |= v >= nv;
        for (const auto& q : mesh->quads)
            for (auto v : q) bad |= v >= nv;
        if (bad) throw IoError(path.filename().string() + ": body \"" + b.name + "\" refers to a vertex that does not exist");
        b.mesh = std::move(mesh);
        bodies.push_back(std::move(b));
    }
    doc.reset(std::move(bodies), manifest.value("nextId", std::uint64_t{1}), path.string());
}

}  // namespace orv

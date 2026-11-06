// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/ThreeMf.h"

#include "core/mesh/MeshIO.h"

#include <miniz.h>

#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <locale>
#include <map>
#include <sstream>
#include <string_view>
#include <vector>

namespace orv {
namespace {

// A ZIP entry larger than this is refused (a "zip bomb" must not fill memory).
constexpr std::uint64_t kMaxEntry = 4ull << 30;

class Zip {
public:
    explicit Zip(const std::string& bytes) {
        if (!mz_zip_reader_init_mem(&zip_, bytes.data(), bytes.size(), 0)) throw IoError("not a 3MF (ZIP) package");
    }
    ~Zip() { mz_zip_reader_end(&zip_); }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;

    bool has(std::string name) const { return find(name) >= 0; }
    std::string read(std::string name) {
        const int i = find(name);
        if (i < 0) throw IoError("the package has no " + name);
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip_, static_cast<mz_uint>(i), &st)) throw IoError("damaged package entry " + name);
        if (st.m_uncomp_size > kMaxEntry) throw IoError(name + " is too large");
        std::string out(static_cast<std::size_t>(st.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&zip_, static_cast<mz_uint>(i), out.data(), out.size(), 0))
            throw IoError("cannot unpack " + name);
        return out;
    }

private:
    int find(std::string name) const {
        if (!name.empty() && name[0] == '/') name.erase(0, 1);
        // Part names are case-insensitive in OPC.
        return mz_zip_reader_locate_file(const_cast<mz_zip_archive*>(&zip_), name.c_str(), nullptr, 0);
    }
    mz_zip_archive zip_{};
};

// ---- a small XML reader: elements and attributes, no DTDs, no entities but
// the five predefined ones. Enough for 3MF, which is plain data.
struct Tag {
    std::string_view name;  // without namespace prefix
    std::string_view prefix;
    bool closing{false}, selfClosing{false};
    std::vector<std::pair<std::string_view, std::string_view>> attrs;
    std::string_view attr(std::string_view n) const {
        for (const auto& [k, v] : attrs) {
            const auto colon = k.find(':');
            if (k == n || (colon != std::string_view::npos && k.substr(colon + 1) == n && k.substr(0, colon) != "xmlns")) return v;
        }
        return {};
    }
    /// Attribute with this exact qualified name (e.g. "p:path").
    std::string_view qattr(std::string_view n) const {
        for (const auto& [k, v] : attrs)
            if (k == n) return v;
        return {};
    }
};

class XmlScanner {
public:
    explicit XmlScanner(std::string_view s) : s_(s) {}
    bool next(Tag& t) {
        while (true) {
            const auto lt = s_.find('<', pos_);
            if (lt == std::string_view::npos) return false;
            pos_ = lt + 1;
            if (s_.compare(pos_, 3, "!--") == 0) { skip("-->"); continue; }
            if (s_.compare(pos_, 8, "![CDATA[") == 0) { skip("]]>"); continue; }
            if (pos_ < s_.size() && (s_[pos_] == '?' || s_[pos_] == '!')) { skip(">"); continue; }
            break;
        }
        t = Tag{};
        if (pos_ < s_.size() && s_[pos_] == '/') { t.closing = true; ++pos_; }
        const auto nameEnd = s_.find_first_of(" \t\r\n/>", pos_);
        if (nameEnd == std::string_view::npos) throw IoError("damaged XML");
        std::string_view qn = s_.substr(pos_, nameEnd - pos_);
        const auto colon = qn.find(':');
        t.name = colon == std::string_view::npos ? qn : qn.substr(colon + 1);
        t.prefix = colon == std::string_view::npos ? std::string_view{} : qn.substr(0, colon);
        pos_ = nameEnd;
        while (true) {
            while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
            if (pos_ >= s_.size()) throw IoError("damaged XML");
            if (s_[pos_] == '>') { ++pos_; return true; }
            if (s_[pos_] == '/') { t.selfClosing = true; pos_ = s_.find('>', pos_); if (pos_ == std::string_view::npos) throw IoError("damaged XML"); ++pos_; return true; }
            const auto eq = s_.find('=', pos_);
            if (eq == std::string_view::npos) throw IoError("damaged XML");
            std::string_view key = s_.substr(pos_, eq - pos_);
            while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.remove_suffix(1);
            auto q = eq + 1;
            while (q < s_.size() && std::isspace(static_cast<unsigned char>(s_[q]))) ++q;
            if (q >= s_.size() || (s_[q] != '"' && s_[q] != '\'')) throw IoError("damaged XML");
            const auto end = s_.find(s_[q], q + 1);
            if (end == std::string_view::npos) throw IoError("damaged XML");
            t.attrs.emplace_back(key, s_.substr(q + 1, end - q - 1));
            pos_ = end + 1;
        }
    }

private:
    void skip(std::string_view until) {
        const auto e = s_.find(until, pos_);
        pos_ = e == std::string_view::npos ? s_.size() : e + until.size();
    }
    std::string_view s_;
    std::size_t pos_{0};
};

double num(std::string_view v, const char* what) {
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.remove_prefix(1);
    double d = 0;
    const auto r = std::from_chars(v.data(), v.data() + v.size(), d);
    if (r.ec != std::errc() || !std::isfinite(d)) throw IoError(std::string("bad number in ") + what);
    return d;
}

std::uint32_t index(std::string_view v, const char* what) {
    std::uint32_t i = 0;
    const auto r = std::from_chars(v.data(), v.data() + v.size(), i);
    if (r.ec != std::errc()) throw IoError(std::string("bad index in ") + what);
    return i;
}

/// 3MF affine transform: row vector times a 4×3 matrix.
using Affine = std::array<double, 12>;
constexpr Affine kIdentity{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

Affine parseTransform(std::string_view v) {
    if (v.empty()) return kIdentity;
    Affine m{};
    std::size_t at = 0;
    for (auto& x : m) {
        while (at < v.size() && std::isspace(static_cast<unsigned char>(v[at]))) ++at;
        const auto r = std::from_chars(v.data() + at, v.data() + v.size(), x);
        if (r.ec != std::errc()) throw IoError("bad transform");
        at = static_cast<std::size_t>(r.ptr - v.data());
    }
    return m;
}

/// a then b (a applied first).
Affine compose(const Affine& a, const Affine& b) {
    Affine r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    for (int j = 0; j < 3; ++j) r[9 + j] = a[9] * b[j] + a[10] * b[3 + j] + a[11] * b[6 + j] + b[9 + j];
    return r;
}

Vec3d apply(const Affine& m, const Vec3d& p) {
    return {p.x() * m[0] + p.y() * m[3] + p.z() * m[6] + m[9],
            p.x() * m[1] + p.y() * m[4] + p.z() * m[7] + m[10],
            p.x() * m[2] + p.y() * m[5] + p.z() * m[8] + m[11]};
}

double unitToMm(std::string_view u) {
    if (u.empty() || u == "millimeter") return 1.0;
    if (u == "micron") return 0.001;
    if (u == "centimeter") return 10.0;
    if (u == "inch") return 25.4;
    if (u == "foot") return 304.8;
    if (u == "meter") return 1000.0;
    throw IoError("unknown unit " + std::string(u));
}

struct Object {
    std::vector<Vec3d> vertices;
    std::vector<Tri> triangles;
    struct Component { std::string path; std::uint32_t id; Affine transform; };
    std::vector<Component> components;
};

struct Model {
    double scale{1.0};
    std::map<std::uint32_t, Object> objects;
    struct Item { std::string path; std::uint32_t id; Affine transform; };
    std::vector<Item> build;
};

Model parseModel(const std::string& xml, const std::string& path) {
    Model m;
    XmlScanner x(xml);
    Tag t;
    Object* obj = nullptr;
    bool inBuild = false;
    while (x.next(t)) {
        if (t.closing) {
            if (t.name == "object") obj = nullptr;
            if (t.name == "build") inBuild = false;
            continue;
        }
        if (t.name == "model") m.scale = unitToMm(t.attr("unit"));
        else if (t.name == "object") {
            obj = &m.objects[index(t.attr("id"), "object id")];
            if (t.selfClosing) obj = nullptr;
        } else if (t.name == "vertex" && obj) {
            obj->vertices.emplace_back(num(t.attr("x"), "vertex"), num(t.attr("y"), "vertex"), num(t.attr("z"), "vertex"));
        } else if (t.name == "triangle" && obj) {
            obj->triangles.push_back({index(t.attr("v1"), "triangle"), index(t.attr("v2"), "triangle"), index(t.attr("v3"), "triangle")});
        } else if (t.name == "component" && obj) {
            std::string p(t.qattr("p:path"));
            if (p.empty()) p = std::string(t.attr("path"));
            obj->components.push_back({p.empty() ? path : p, index(t.attr("objectid"), "component"), parseTransform(t.attr("transform"))});
        } else if (t.name == "build") {
            inBuild = !t.selfClosing;
        } else if (t.name == "item" && inBuild) {
            std::string p(t.qattr("p:path"));
            if (p.empty()) p = std::string(t.attr("path"));
            m.build.push_back({p.empty() ? path : p, index(t.attr("objectid"), "build item"), parseTransform(t.attr("transform"))});
        }
    }
    return m;
}

std::string lowerName(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!s.empty() && s[0] != '/') s.insert(s.begin(), '/');
    return s;
}

class Reader {
public:
    explicit Reader(const std::string& bytes) : zip_(bytes) {}

    TriMesh run() {
        const std::string root = rootModel();
        const Model& m = model(root);
        scale_ = m.scale;  // the package's unit is the root model's
        if (m.build.empty()) throw IoError("the 3MF has nothing to build");
        for (const auto& it : m.build) add(it.path, it.id, it.transform, 0);
        if (out_.faces.empty()) throw IoError("the 3MF has no triangles");
        return std::move(out_);
    }

private:
    std::string rootModel() {
        std::string target = "/3D/3dmodel.model";
        if (zip_.has("_rels/.rels")) {
            const std::string rels = zip_.read("_rels/.rels");
            XmlScanner x(rels);
            Tag t;
            while (x.next(t))
                if (t.name == "Relationship" && t.attr("Type").find("3dmodel") != std::string_view::npos) {
                    target = std::string(t.attr("Target"));
                    break;
                }
        }
        return target;
    }

    const Model& model(const std::string& path) {
        const std::string key = lowerName(path);
        auto it = models_.find(key);
        if (it != models_.end()) return it->second;
        return models_.emplace(key, parseModel(zip_.read(path), path)).first->second;
    }

    void add(const std::string& path, std::uint32_t id, const Affine& transform, int depth) {
        if (depth > 32) throw IoError("components nest too deep (a cycle?)");
        const Model& m = model(path);
        const auto found = m.objects.find(id);
        if (found == m.objects.end()) throw IoError("build refers to object " + std::to_string(id) + " that does not exist");
        const Object& o = found->second;
        const Affine& t = transform;
        if (!o.triangles.empty()) {
            const auto base = static_cast<std::uint32_t>(out_.vertices.size());
            for (const auto& p : o.vertices) out_.vertices.push_back((apply(t, p) * scale_).cast<float>());
            for (const Tri& tr : o.triangles) {
                for (auto i : tr)
                    if (i >= o.vertices.size()) throw IoError("a triangle refers to a vertex that does not exist");
                if (tr[0] == tr[1] || tr[1] == tr[2] || tr[0] == tr[2]) continue;
                out_.faces.push_back({base + tr[0], base + tr[1], base + tr[2]});
            }
        }
        for (const auto& c : o.components) add(c.path, c.id, compose(c.transform, t), depth + 1);
    }

    Zip zip_;
    std::map<std::string, Model> models_;
    TriMesh out_;
    double scale_{1.0};
};

}  // namespace

TriMesh read3mf(const std::string& bytes) { return Reader(bytes).run(); }

void write3mf(const TriMesh& mesh, const std::filesystem::path& path) {
    std::ostringstream xml;
    xml.imbue(std::locale::classic());
    xml.precision(9);
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
           " <metadata name=\"Application\">OpenRevers</metadata>\n"
           " <resources>\n  <object id=\"1\" type=\"model\">\n   <mesh>\n    <vertices>\n";
    for (const auto& v : mesh.vertices) xml << "     <vertex x=\"" << v.x() << "\" y=\"" << v.y() << "\" z=\"" << v.z() << "\"/>\n";
    xml << "    </vertices>\n    <triangles>\n";
    for (const auto& t : mesh.faces) xml << "     <triangle v1=\"" << t[0] << "\" v2=\"" << t[1] << "\" v3=\"" << t[2] << "\"/>\n";
    xml << "    </triangles>\n   </mesh>\n  </object>\n </resources>\n <build>\n  <item objectid=\"1\"/>\n </build>\n</model>\n";
    const std::string model = xml.str();
    const std::string types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/></Types>\n";
    const std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>\n";

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) throw IoError("Cannot write " + path.string());
    bool ok = mz_zip_writer_add_mem(&zip, "[Content_Types].xml", types.data(), types.size(), MZ_DEFAULT_COMPRESSION) &&
              mz_zip_writer_add_mem(&zip, "_rels/.rels", rels.data(), rels.size(), MZ_DEFAULT_COMPRESSION) &&
              mz_zip_writer_add_mem(&zip, "3D/3dmodel.model", model.data(), model.size(), MZ_DEFAULT_COMPRESSION);
    void* buf = nullptr;
    std::size_t size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buf, &size);
    std::string bytes;
    if (ok) bytes.assign(static_cast<const char*>(buf), size);
    mz_zip_writer_end(&zip);
    if (!ok) throw IoError("Cannot pack " + path.string());
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw IoError("Failed while writing " + path.string());
}

}  // namespace orv

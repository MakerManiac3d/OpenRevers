// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/MeshIO.h"

#include "core/mesh/ThreeMf.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace orv {
namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw IoError("Cannot open " + path.string());
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0);
    std::string data(size, '\0');
    if (size && !in.read(data.data(), static_cast<std::streamsize>(size))) throw IoError("Cannot read " + path.string());
    return data;
}

/// Joins exactly coincident points into one vertex (STL stores every corner).
class Welder {
public:
    explicit Welder(TriMesh& m) : mesh_(m) {}
    std::uint32_t add(const Vec3f& p) {
        Key k{std::bit_cast<std::uint32_t>(p.x() == 0.f ? 0.f : p.x()), std::bit_cast<std::uint32_t>(p.y() == 0.f ? 0.f : p.y()),
              std::bit_cast<std::uint32_t>(p.z() == 0.f ? 0.f : p.z())};
        auto [it, inserted] = index_.try_emplace(k, static_cast<std::uint32_t>(mesh_.vertices.size()));
        if (inserted) mesh_.vertices.push_back(p);
        return it->second;
    }
    void reserve(std::size_t n) { index_.reserve(n); }

private:
    struct Key {
        std::uint32_t x, y, z;
        bool operator==(const Key&) const = default;
    };
    struct Hash {
        std::size_t operator()(const Key& k) const noexcept {
            std::uint64_t h = k.x * 0x9E3779B97F4A7C15ull;
            h ^= (k.y + 0x7F4A7C15ull + (h << 6) + (h >> 2)) * 0xBF58476D1CE4E5B9ull;
            h ^= (k.z + 0x94D049BBull + (h << 6) + (h >> 2)) * 0x94D049BB133111EBull;
            return static_cast<std::size_t>(h ^ (h >> 31));
        }
    };
    TriMesh& mesh_;
    std::unordered_map<Key, std::uint32_t, Hash> index_;
};

void addFace(TriMesh& m, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (a != b && b != c && a != c) m.faces.push_back({a, b, c});
}

/// Collects the polygons of an OBJ or PLY file: triangulated as they come,
/// and kept as quads too while every polygon is a quad.
struct PolygonSink {
    TriMesh& m;
    std::vector<Quad> quads;
    bool allQuads{true};
    void add(const std::vector<long>& poly) {
        bool quad = poly.size() == 4;
        for (std::size_t a = 0; a < poly.size() && quad; ++a)
            for (std::size_t b = a + 1; b < poly.size() && quad; ++b) quad = poly[a] != poly[b];
        if (quad && allQuads)
            quads.push_back({static_cast<std::uint32_t>(poly[0]), static_cast<std::uint32_t>(poly[1]),
                             static_cast<std::uint32_t>(poly[2]), static_cast<std::uint32_t>(poly[3])});
        else allQuads = false;
        for (std::size_t i = 1; i + 1 < poly.size(); ++i)
            addFace(m, static_cast<std::uint32_t>(poly[0]), static_cast<std::uint32_t>(poly[i]), static_cast<std::uint32_t>(poly[i + 1]));
    }
    void finish() {
        if (allQuads && !quads.empty()) m.setQuads(std::move(quads));
    }
};

// ---------------------------------------------------------------- STL -----
bool looksBinaryStl(const std::string& d) {
    if (d.size() < 84) return false;
    std::uint32_t n;
    std::memcpy(&n, d.data() + 80, 4);
    return 84ull + 50ull * n == d.size();
}

TriMesh readBinaryStl(const std::string& d) {
    TriMesh m;
    std::uint32_t n;
    std::memcpy(&n, d.data() + 80, 4);
    m.faces.reserve(n);
    m.vertices.reserve(n / 2 + 8);
    Welder w(m);
    w.reserve(n / 2 + 8);
    const char* p = d.data() + 84;
    for (std::uint32_t i = 0; i < n; ++i, p += 50) {
        float f[12];
        std::memcpy(f, p, 48);
        const std::uint32_t a = w.add({f[3], f[4], f[5]});
        const std::uint32_t b = w.add({f[6], f[7], f[8]});
        const std::uint32_t c = w.add({f[9], f[10], f[11]});
        addFace(m, a, b, c);
    }
    return m;
}

TriMesh readAsciiStl(const std::string& d) {
    TriMesh m;
    Welder w(m);
    std::istringstream in(d);
    std::string tok;
    std::uint32_t corner[3];
    int k = 0;
    while (in >> tok) {
        if (lower(tok) != "vertex") continue;
        float x, y, z;
        if (!(in >> x >> y >> z)) throw IoError("Malformed vertex in ASCII STL");
        corner[k++] = w.add({x, y, z});
        if (k == 3) {
            addFace(m, corner[0], corner[1], corner[2]);
            k = 0;
        }
    }
    return m;
}

// ---------------------------------------------------------------- OBJ -----
TriMesh readObj(const std::string& d) {
    TriMesh m;
    PolygonSink sink{m, {}, true};
    std::istringstream in(d);
    std::string line;
    std::vector<long> poly;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && (line[1] == ' ' || line[1] == '\t')) {
            std::istringstream ls(line.substr(2));
            float x = 0, y = 0, z = 0;
            ls >> x >> y >> z;
            m.vertices.emplace_back(x, y, z);
        } else if (line[0] == 'v' && line[1] == 'n' && line.size() > 2 && (line[2] == ' ' || line[2] == '\t')) {
            std::istringstream ls(line.substr(3));
            float x = 0, y = 0, z = 0;
            ls >> x >> y >> z;
            m.normals.emplace_back(x, y, z);
        } else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t')) {
            std::istringstream ls(line.substr(2));
            std::string item;
            poly.clear();
            while (ls >> item) {
                const auto slash = item.find('/');
                const long idx = std::stol(item.substr(0, slash));
                const long n = static_cast<long>(m.vertices.size());
                const long v = idx > 0 ? idx - 1 : n + idx;
                if (v < 0 || v >= n) throw IoError("OBJ face refers to a vertex that does not exist");
                poly.push_back(v);
            }
            sink.add(poly);
        }
    }
    sink.finish();
    // Per-corner normals of a mesh are not kept; a cloud's "vn" lines go with its "v" lines.
    if (!m.faces.empty() || m.normals.size() != m.vertices.size()) m.normals.clear();
    return m;
}

// ---------------------------------------------------------------- XYZ -----
// Text points: "x y z" or "x y z nx ny nz" per line, separated by spaces,
// tabs, commas or semicolons. Other column counts (intensity, colour) keep
// only x y z. Lines starting with # or // and a .pts point-count line are
// skipped.
TriMesh readXyz(const std::string& d) {
    TriMesh m;
    std::size_t pos = 0, lineNo = 0;
    bool normals = true;
    std::vector<double> vals;
    while (pos < d.size()) {
        std::size_t end = d.find('\n', pos);
        if (end == std::string::npos) end = d.size();
        std::string_view line(d.data() + pos, end - pos);
        pos = end + 1;
        ++lineNo;
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1);
        if (line.empty() || line[0] == '#' || line.rfind("//", 0) == 0) continue;
        vals.clear();
        std::size_t at = 0;
        while (at < line.size()) {
            while (at < line.size() && (std::isspace(static_cast<unsigned char>(line[at])) || line[at] == ',' || line[at] == ';')) ++at;
            if (at >= line.size()) break;
            double v = 0;
            const char* b = line.data() + at;
            if (*b == '+') ++b;  // from_chars does not take a leading '+'
            const auto r = std::from_chars(b, line.data() + line.size(), v);
            if (r.ec != std::errc()) throw IoError("line " + std::to_string(lineNo) + " is not numbers");
            vals.push_back(v);
            at = static_cast<std::size_t>(r.ptr - line.data());
        }
        if (vals.size() == 1 && m.vertices.empty()) continue;  // .pts: the number of points
        if (vals.size() < 3) throw IoError("line " + std::to_string(lineNo) + " has fewer than 3 numbers");
        m.vertices.emplace_back(static_cast<float>(vals[0]), static_cast<float>(vals[1]), static_cast<float>(vals[2]));
        if (vals.size() == 6) m.normals.emplace_back(static_cast<float>(vals[3]), static_cast<float>(vals[4]), static_cast<float>(vals[5]));
        else normals = false;
    }
    if (!normals) m.normals.clear();
    return m;
}

// ---------------------------------------------------------------- PLY -----
enum class PlyType { I8, U8, I16, U16, I32, U32, F32, F64 };

PlyType plyType(const std::string& t) {
    const std::string s = lower(t);
    if (s == "char" || s == "int8") return PlyType::I8;
    if (s == "uchar" || s == "uint8") return PlyType::U8;
    if (s == "short" || s == "int16") return PlyType::I16;
    if (s == "ushort" || s == "uint16") return PlyType::U16;
    if (s == "int" || s == "int32") return PlyType::I32;
    if (s == "uint" || s == "uint32") return PlyType::U32;
    if (s == "float" || s == "float32") return PlyType::F32;
    if (s == "double" || s == "float64") return PlyType::F64;
    throw IoError("Unknown PLY property type: " + t);
}

std::size_t plySize(PlyType t) {
    switch (t) {
        case PlyType::I8: case PlyType::U8: return 1;
        case PlyType::I16: case PlyType::U16: return 2;
        case PlyType::I32: case PlyType::U32: case PlyType::F32: return 4;
        case PlyType::F64: return 8;
    }
    return 0;
}

struct PlyProperty {
    std::string name;
    PlyType type{PlyType::F32};
    bool list{false};
    PlyType countType{PlyType::U8};
};
struct PlyElement {
    std::string name;
    std::size_t count{0};
    std::vector<PlyProperty> props;
};

class PlyReader {
public:
    PlyReader(const std::string& d, std::size_t pos, bool binary, bool bigEndian)
        : d_(d), pos_(pos), binary_(binary), big_(bigEndian) {
        if (!binary_) text_.str(d_.substr(pos_));
    }
    double read(PlyType t) {
        if (!binary_) {
            double v;
            if (!(text_ >> v)) throw IoError("PLY data ended early");
            return v;
        }
        const std::size_t n = plySize(t);
        if (pos_ + n > d_.size()) throw IoError("PLY data ended early");
        unsigned char b[8];
        std::memcpy(b, d_.data() + pos_, n);
        pos_ += n;
        if (big_ != (std::endian::native == std::endian::big)) std::reverse(b, b + n);
        switch (t) {
            case PlyType::I8: { std::int8_t v; std::memcpy(&v, b, 1); return v; }
            case PlyType::U8: { std::uint8_t v; std::memcpy(&v, b, 1); return v; }
            case PlyType::I16: { std::int16_t v; std::memcpy(&v, b, 2); return v; }
            case PlyType::U16: { std::uint16_t v; std::memcpy(&v, b, 2); return v; }
            case PlyType::I32: { std::int32_t v; std::memcpy(&v, b, 4); return v; }
            case PlyType::U32: { std::uint32_t v; std::memcpy(&v, b, 4); return v; }
            case PlyType::F32: { float v; std::memcpy(&v, b, 4); return v; }
            case PlyType::F64: { double v; std::memcpy(&v, b, 8); return v; }
        }
        return 0;
    }

private:
    const std::string& d_;
    std::size_t pos_;
    bool binary_, big_;
    std::istringstream text_;
};

TriMesh readPly(const std::string& d) {
    const auto end = d.find("end_header");
    if (d.compare(0, 3, "ply") != 0 || end == std::string::npos) throw IoError("Not a PLY file");
    std::istringstream header(d.substr(0, end));
    std::string line, format;
    std::vector<PlyElement> elements;
    while (std::getline(header, line)) {
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;
        if (kw == "format") ls >> format;
        else if (kw == "element") {
            PlyElement e;
            ls >> e.name >> e.count;
            elements.push_back(e);
        } else if (kw == "property" && !elements.empty()) {
            PlyProperty p;
            std::string t;
            ls >> t;
            if (t == "list") {
                std::string ct, it;
                ls >> ct >> it >> p.name;
                p.list = true;
                p.countType = plyType(ct);
                p.type = plyType(it);
            } else {
                p.type = plyType(t);
                ls >> p.name;
            }
            elements.back().props.push_back(p);
        }
    }
    std::size_t pos = end + 10;  // past "end_header"
    if (pos < d.size() && d[pos] == '\r') ++pos;
    if (pos < d.size() && d[pos] == '\n') ++pos;
    const bool binary = format != "ascii";
    PlyReader r(d, pos, binary, format == "binary_big_endian");

    TriMesh m;
    PolygonSink sink{m, {}, true};
    for (const auto& e : elements) {
        if (e.name == "vertex") {
            int ix = -1, iy = -1, iz = -1, inx = -1, iny = -1, inz = -1;
            for (std::size_t i = 0; i < e.props.size(); ++i) {
                if (e.props[i].name == "x") ix = static_cast<int>(i);
                if (e.props[i].name == "y") iy = static_cast<int>(i);
                if (e.props[i].name == "z") iz = static_cast<int>(i);
                if (e.props[i].name == "nx") inx = static_cast<int>(i);
                if (e.props[i].name == "ny") iny = static_cast<int>(i);
                if (e.props[i].name == "nz") inz = static_cast<int>(i);
            }
            const bool withNormals = inx >= 0 && iny >= 0 && inz >= 0;
            if (withNormals) m.normals.reserve(e.count);
            if (ix < 0 || iy < 0 || iz < 0) throw IoError("PLY vertices have no x, y, z");
            m.vertices.reserve(e.count);
            std::vector<double> vals(e.props.size());
            for (std::size_t v = 0; v < e.count; ++v) {
                for (std::size_t i = 0; i < e.props.size(); ++i) {
                    const auto& p = e.props[i];
                    if (p.list) {
                        const auto n = static_cast<std::size_t>(r.read(p.countType));
                        for (std::size_t j = 0; j < n; ++j) r.read(p.type);
                        vals[i] = 0;
                    } else vals[i] = r.read(p.type);
                }
                m.vertices.emplace_back(static_cast<float>(vals[ix]), static_cast<float>(vals[iy]), static_cast<float>(vals[iz]));
                if (withNormals) m.normals.emplace_back(static_cast<float>(vals[inx]), static_cast<float>(vals[iny]), static_cast<float>(vals[inz]));
            }
        } else if (e.name == "face") {
            m.faces.reserve(e.count);
            std::vector<long> poly;
            for (std::size_t f = 0; f < e.count; ++f) {
                for (const auto& p : e.props) {
                    if (!p.list) { r.read(p.type); continue; }
                    const auto n = static_cast<std::size_t>(r.read(p.countType));
                    poly.assign(n, 0);
                    for (std::size_t j = 0; j < n; ++j) poly[j] = static_cast<long>(r.read(p.type));
                    if (p.name != "vertex_indices" && p.name != "vertex_index") continue;
                    for (long v : poly)
                        if (v < 0 || static_cast<std::size_t>(v) >= m.vertices.size()) throw IoError("PLY face refers to a vertex that does not exist");
                    sink.add(poly);
                }
            }
        } else {
            for (std::size_t k = 0; k < e.count; ++k)
                for (const auto& p : e.props) {
                    if (p.list) {
                        const auto n = static_cast<std::size_t>(r.read(p.countType));
                        for (std::size_t j = 0; j < n; ++j) r.read(p.type);
                    } else r.read(p.type);
                }
        }
    }
    sink.finish();
    if (!m.faces.empty()) m.normals.clear();  // only a point cloud keeps normals
    return m;
}

// ------------------------------------------------------------- writers ----
void writeBinaryStl(const TriMesh& m, std::ofstream& out) {
    char header[80] = {};
    std::strncpy(header, "OpenRevers binary STL (mm)", sizeof(header) - 1);
    out.write(header, 80);
    const auto n = static_cast<std::uint32_t>(m.faces.size());
    out.write(reinterpret_cast<const char*>(&n), 4);
    for (std::size_t f = 0; f < m.faces.size(); ++f) {
        float buf[12];
        const Vec3f nrm = m.faceNormal(f);
        std::memcpy(buf, nrm.data(), 12);
        for (int k = 0; k < 3; ++k) std::memcpy(buf + 3 + 3 * k, m.vertices[m.faces[f][k]].data(), 12);
        out.write(reinterpret_cast<const char*>(buf), 48);
        const std::uint16_t attr = 0;
        out.write(reinterpret_cast<const char*>(&attr), 2);
    }
}

void writeAsciiStl(const TriMesh& m, std::ofstream& out) {
    out << "solid openrevers\n";
    out.precision(9);
    for (std::size_t f = 0; f < m.faces.size(); ++f) {
        const Vec3f n = m.faceNormal(f);
        out << " facet normal " << n.x() << ' ' << n.y() << ' ' << n.z() << "\n  outer loop\n";
        for (auto i : m.faces[f]) {
            const Vec3f& p = m.vertices[i];
            out << "   vertex " << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
        }
        out << "  endloop\n endfacet\n";
    }
    out << "endsolid openrevers\n";
}

void writeObj(const TriMesh& m, std::ofstream& out) {
    out << "# OpenRevers OBJ (mm)\n";
    out.precision(9);
    for (const auto& p : m.vertices) out << "v " << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
    if (m.isPointCloud()) {
        if (m.hasNormals())
            for (const auto& n : m.normals) out << "vn " << n.x() << ' ' << n.y() << ' ' << n.z() << '\n';
        return;
    }
    if (m.hasQuads()) {
        for (const auto& q : m.quads) out << "f " << q[0] + 1 << ' ' << q[1] + 1 << ' ' << q[2] + 1 << ' ' << q[3] + 1 << '\n';
        return;
    }
    for (const auto& t : m.faces) out << "f " << t[0] + 1 << ' ' << t[1] + 1 << ' ' << t[2] + 1 << '\n';
}

void writePlyPoints(const TriMesh& m, std::ofstream& out, bool ascii) {
    const bool n = m.hasNormals();
    out << "ply\nformat " << (ascii ? "ascii" : "binary_little_endian") << " 1.0\ncomment OpenRevers (mm)\n"
        << "element vertex " << m.vertices.size() << "\nproperty float x\nproperty float y\nproperty float z\n"
        << (n ? "property float nx\nproperty float ny\nproperty float nz\n" : "") << "end_header\n";
    out.precision(9);
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const Vec3f& p = m.vertices[i];
        if (ascii) {
            out << p.x() << ' ' << p.y() << ' ' << p.z();
            if (n) out << ' ' << m.normals[i].x() << ' ' << m.normals[i].y() << ' ' << m.normals[i].z();
            out << '\n';
        } else {
            out.write(reinterpret_cast<const char*>(p.data()), 12);
            if (n) out.write(reinterpret_cast<const char*>(m.normals[i].data()), 12);
        }
    }
}

void writeXyz(const TriMesh& m, std::ofstream& out) {
    out.precision(9);
    const bool n = m.hasNormals();
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const Vec3f& p = m.vertices[i];
        out << p.x() << ' ' << p.y() << ' ' << p.z();
        if (n) out << ' ' << m.normals[i].x() << ' ' << m.normals[i].y() << ' ' << m.normals[i].z();
        out << '\n';
    }
}

void writePly(const TriMesh& m, std::ofstream& out, bool ascii) {
    if (m.isPointCloud()) {
        writePlyPoints(m, out, ascii);
        return;
    }
    out << "ply\nformat " << (ascii ? "ascii" : "binary_little_endian") << " 1.0\ncomment OpenRevers (mm)\n"
        << "element vertex " << m.vertices.size() << "\nproperty float x\nproperty float y\nproperty float z\n"
        << "element face " << (m.hasQuads() ? m.quads.size() : m.faces.size()) << "\nproperty list uchar uint vertex_indices\nend_header\n";
    if (ascii) {
        out.precision(9);
        for (const auto& p : m.vertices) out << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
        if (m.hasQuads())
            for (const auto& q : m.quads) out << "4 " << q[0] << ' ' << q[1] << ' ' << q[2] << ' ' << q[3] << '\n';
        else
            for (const auto& t : m.faces) out << "3 " << t[0] << ' ' << t[1] << ' ' << t[2] << '\n';
        return;
    }
    static_assert(std::endian::native == std::endian::little, "PLY writer assumes a little-endian machine");
    for (const auto& p : m.vertices) out.write(reinterpret_cast<const char*>(p.data()), 12);
    if (m.hasQuads()) {
        for (const auto& q : m.quads) {
            const std::uint8_t four = 4;
            out.write(reinterpret_cast<const char*>(&four), 1);
            out.write(reinterpret_cast<const char*>(q.data()), 16);
        }
        return;
    }
    for (const auto& t : m.faces) {
        const std::uint8_t three = 3;
        out.write(reinterpret_cast<const char*>(&three), 1);
        out.write(reinterpret_cast<const char*>(t.data()), 12);
    }
}

}  // namespace

MeshFormat formatFromPath(const fs::path& path) {
    const std::string ext = lower(path.extension().string());
    if (ext == ".stl") return MeshFormat::Stl;
    if (ext == ".obj") return MeshFormat::Obj;
    if (ext == ".ply") return MeshFormat::Ply;
    if (ext == ".3mf") return MeshFormat::ThreeMf;
    if (ext == ".xyz" || ext == ".pts") return MeshFormat::Xyz;
    throw IoError("Unsupported mesh format: " + (ext.empty() ? path.filename().string() : ext) + " (use .stl, .obj, .ply, .3mf, .xyz or .pts)");
}

TriMesh readMesh(const fs::path& path) {
    const MeshFormat fmt = formatFromPath(path);
    const std::string d = readAll(path);
    TriMesh m;
    try {
        switch (fmt) {
            case MeshFormat::Stl: case MeshFormat::StlAscii: m = looksBinaryStl(d) ? readBinaryStl(d) : readAsciiStl(d); break;
            case MeshFormat::Obj: m = readObj(d); break;
            case MeshFormat::Ply: case MeshFormat::PlyAscii: m = readPly(d); break;
            case MeshFormat::ThreeMf: m = read3mf(d); break;
            case MeshFormat::Xyz: m = readXyz(d); break;
        }
    } catch (const IoError& e) {
        throw IoError(path.filename().string() + ": " + e.what());
    } catch (const std::exception& e) {
        throw IoError(path.filename().string() + ": malformed file (" + e.what() + ")");
    }
    if (m.vertices.empty()) throw IoError(path.filename().string() + ": the file has no points or triangles");
    // STL and 3MF are always meshes; a PLY, OBJ or XYZ without faces is a point cloud.
    if (m.faces.empty() && (fmt == MeshFormat::Stl || fmt == MeshFormat::StlAscii || fmt == MeshFormat::ThreeMf))
        throw IoError(path.filename().string() + ": the file has no triangles");
    for (const auto& p : m.vertices)
        if (!p.allFinite()) throw IoError(path.filename().string() + ": a coordinate is not a finite number");
    m.removeUnusedVertices();
    return m;
}

void writeMesh(const TriMesh& mesh, const fs::path& path) { writeMesh(mesh, path, formatFromPath(path)); }

void writeMesh(const TriMesh& mesh, const fs::path& path, MeshFormat format) {
    if (mesh.isPointCloud() && (format == MeshFormat::Stl || format == MeshFormat::StlAscii || format == MeshFormat::ThreeMf))
        throw IoError("A point cloud has no triangles to write as STL or 3MF; use .ply, .xyz or .obj, or reconstruct a mesh first");
    if (format == MeshFormat::Xyz && !mesh.isPointCloud())
        throw IoError("XYZ holds points only; write a mesh as .stl, .obj, .ply or .3mf");
    if (format == MeshFormat::ThreeMf) {
        write3mf(mesh, path);
        return;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) throw IoError("Cannot write " + path.string());
    switch (format) {
        case MeshFormat::Stl: writeBinaryStl(mesh, out); break;
        case MeshFormat::StlAscii: writeAsciiStl(mesh, out); break;
        case MeshFormat::Obj: writeObj(mesh, out); break;
        case MeshFormat::Ply: writePly(mesh, out, false); break;
        case MeshFormat::PlyAscii: writePly(mesh, out, true); break;
        case MeshFormat::Xyz: writeXyz(mesh, out); break;
        case MeshFormat::ThreeMf: break;
    }
    if (!out) throw IoError("Failed while writing " + path.string());
}

}  // namespace orv

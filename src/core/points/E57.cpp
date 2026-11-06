// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/points/E57.h"

#include "core/mesh/MeshIO.h"
#include "core/util/Xml.h"

#include <Eigen/Geometry>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <string_view>

namespace orv {
namespace {

// ------------------------------------------------------------ the pages --
// An E57 file is a sequence of pages; the last 4 bytes of each are a
// CRC-32C of the rest. Offsets in the file are physical (with the CRCs);
// lengths of sections are logical (without).

std::uint32_t crc32c(const unsigned char* p, std::size_t n) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0x82F63B78u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

class Pages {
public:
    explicit Pages(const std::string& file) : d_(file) {
        if (d_.size() < 48 || std::memcmp(d_.data(), "ASTM-E57", 8) != 0) throw IoError("not an E57 file");
        std::uint32_t major = 0;
        std::memcpy(&major, d_.data() + 8, 4);
        std::uint64_t physicalLength = 0;
        std::memcpy(&physicalLength, d_.data() + 16, 8);
        std::memcpy(&xmlOffset_, d_.data() + 24, 8);
        std::memcpy(&xmlLength_, d_.data() + 32, 8);
        std::memcpy(&page_, d_.data() + 40, 8);
        if (major != 1) throw IoError("E57 version " + std::to_string(major) + " is not supported");
        if (page_ < 64 || page_ > (1u << 20) || physicalLength != d_.size() || d_.size() % page_ != 0)
            throw IoError("damaged E57 header (page size or length)");
        // Every page must carry a correct checksum. The CRC is stored big-endian.
        const auto* u = reinterpret_cast<const unsigned char*>(d_.data());
        for (std::uint64_t p = 0; p < d_.size(); p += page_) {
            const std::uint32_t stored = (std::uint32_t{u[p + page_ - 4]} << 24) | (std::uint32_t{u[p + page_ - 3]} << 16) |
                                         (std::uint32_t{u[p + page_ - 2]} << 8) | u[p + page_ - 1];
            if (crc32c(u + p, page_ - 4) != stored) throw IoError("damaged E57 file: checksum error on page " + std::to_string(p / page_));
        }
    }

    std::string xml() const { return read(xmlOffset_, xmlLength_); }

    /// Read `n` logical bytes starting at physical offset `physical`.
    std::string read(std::uint64_t physical, std::uint64_t n) const {
        if (n > d_.size()) throw IoError("damaged E57 file: section longer than the file");
        std::string out;
        out.reserve(n);
        std::uint64_t at = physical;
        while (out.size() < n) {
            const std::uint64_t inPage = at % page_;
            if (inPage >= page_ - 4) { at += page_ - inPage; continue; }  // skip a CRC
            if (at >= d_.size()) throw IoError("damaged E57 file: read past the end");
            const std::uint64_t take = std::min<std::uint64_t>(page_ - 4 - inPage, n - out.size());
            out.append(d_.data() + at, take);
            at += take;
        }
        return out;
    }

    /// Logical position (CRCs not counted) of a physical offset.
    std::uint64_t logical(std::uint64_t physical) const { return physical / page_ * (page_ - 4) + std::min(physical % page_, page_ - 4); }

    /// Physical offset `logical` bytes after physical offset `physical`.
    std::uint64_t advance(std::uint64_t physical, std::uint64_t logical) const {
        std::uint64_t at = physical;
        while (logical > 0) {
            const std::uint64_t inPage = at % page_;
            if (inPage >= page_ - 4) { at += page_ - inPage; continue; }
            const std::uint64_t take = std::min<std::uint64_t>(page_ - 4 - inPage, logical);
            at += take;
            logical -= take;
        }
        return at;
    }

private:
    const std::string& d_;
    std::uint64_t xmlOffset_{0}, xmlLength_{0}, page_{1024};
};

// ---------------------------------------------------------- the fields --

enum class Kind { Float, Double, Integer, Scaled };

struct Field {
    std::string name;
    Kind kind{Kind::Double};
    std::int64_t min{0}, max{0};
    double scale{1.0}, offset{0.0};
    int bits{0};
};

std::int64_t intAttr(const xml::Element& e, const char* n, std::int64_t def) {
    const std::string* v = e.attribute(n);
    if (!v || v->empty()) return def;
    try {
        return std::stoll(*v);
    } catch (...) {
        throw IoError("bad number in E57 attribute " + std::string(n));
    }
}

double dblAttr(const xml::Element& e, const char* n, double def) {
    const std::string* v = e.attribute(n);
    if (!v || v->empty()) return def;
    try {
        return std::stod(*v);
    } catch (...) {
        throw IoError("bad number in E57 attribute " + std::string(n));
    }
}

/// Value of a Float or Integer element (empty means 0).
double number(const xml::Element* e) {
    if (!e) return 0.0;
    std::string t = e->text;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    if (t.find_first_not_of(" \t\r\n") == std::string::npos) return 0.0;
    try {
        return std::stod(t);
    } catch (...) {
        throw IoError("bad number in E57 element " + e->name);
    }
}

Field makeField(const xml::Element& e) {
    Field f;
    f.name = e.name;
    const std::string* type = e.attribute("type");
    if (!type) throw IoError("E57 field " + e.name + " has no type");
    if (*type == "Float") {
        const std::string* p = e.attribute("precision");
        f.kind = p && *p == "single" ? Kind::Float : Kind::Double;
        return f;
    }
    if (*type == "Integer" || *type == "ScaledInteger") {
        f.kind = *type == "Integer" ? Kind::Integer : Kind::Scaled;
        f.min = intAttr(e, "minimum", std::numeric_limits<std::int64_t>::min());
        f.max = intAttr(e, "maximum", std::numeric_limits<std::int64_t>::max());
        if (f.max < f.min) throw IoError("E57 field " + e.name + ": maximum below minimum");
        if (f.kind == Kind::Scaled) {
            f.scale = dblAttr(e, "scale", 1.0);
            f.offset = dblAttr(e, "offset", 0.0);
        }
        const auto range = static_cast<std::uint64_t>(f.max) - static_cast<std::uint64_t>(f.min);
        f.bits = 0;
        while (f.bits < 64 && (range >> f.bits) != 0) ++f.bits;
        return f;
    }
    throw IoError("E57 field " + e.name + " has an unsupported type " + *type);
}

/// Values of one field, read from its bytestream.
std::vector<double> decode(const Field& f, const std::string& s, std::size_t count) {
    // Check the stream holds every record before allocating for them.
    const std::size_t bitsEach = f.kind == Kind::Float ? 32 : f.kind == Kind::Double ? 64 : static_cast<std::size_t>(f.bits);
    if (s.size() * 8 < count * bitsEach) throw IoError("E57 field " + f.name + " is shorter than its record count");
    std::vector<double> out(count);
    if (f.kind == Kind::Float || f.kind == Kind::Double) {
        const std::size_t w = f.kind == Kind::Float ? 4 : 8;
        for (std::size_t i = 0; i < count; ++i) {
            if (w == 4) { float v; std::memcpy(&v, s.data() + i * 4, 4); out[i] = v; }
            else { double v; std::memcpy(&v, s.data() + i * 8, 8); out[i] = v; }
        }
        return out;
    }
    // Integers are bit-packed, least significant bit first, (max - min) wide.
    if (f.bits == 0) {
        std::fill(out.begin(), out.end(), static_cast<double>(f.min));
    } else {
        const auto* u = reinterpret_cast<const unsigned char*>(s.data());
        std::size_t bit = 0;
        for (std::size_t i = 0; i < count; ++i) {
            std::uint64_t raw = 0;
            for (int got = 0; got < f.bits;) {
                const std::size_t byte = bit >> 3;
                const int shift = static_cast<int>(bit & 7);
                const int take = std::min(8 - shift, f.bits - got);
                const std::uint64_t part = (u[byte] >> shift) & ((1u << take) - 1u);
                raw |= part << got;
                got += take;
                bit += static_cast<std::size_t>(take);
            }
            const auto v = static_cast<std::int64_t>(static_cast<std::uint64_t>(f.min) + raw);
            out[i] = static_cast<double>(v);
        }
    }
    if (f.kind == Kind::Scaled)
        for (double& v : out) v = v * f.scale + f.offset;
    return out;
}

/// The bytestreams of a compressed vector, each with all its packets joined.
std::vector<std::string> bytestreams(const Pages& pages, std::uint64_t sectionPhysical, std::size_t fieldCount) {
    const std::string head = pages.read(sectionPhysical, 32);
    if (static_cast<unsigned char>(head[0]) != 1) throw IoError("damaged E57 file: not a compressed vector section");
    std::uint64_t sectionLength = 0, dataPhysical = 0;
    std::memcpy(&sectionLength, head.data() + 8, 8);
    std::memcpy(&dataPhysical, head.data() + 16, 8);
    if (sectionLength < 32) throw IoError("damaged E57 file: bad section length");
    if (pages.logical(dataPhysical) < pages.logical(sectionPhysical) + 32) throw IoError("damaged E57 file: data before its section header");
    const std::uint64_t gap = pages.logical(dataPhysical) - pages.logical(sectionPhysical);
    if (gap > sectionLength) throw IoError("damaged E57 file: data past its section");
    std::vector<std::string> streams(fieldCount);
    std::uint64_t at = dataPhysical;
    std::uint64_t left = sectionLength - gap;  // the packets: index, data and empty
    while (left >= 4) {
        const std::string ph = pages.read(at, 4);
        const auto type = static_cast<unsigned char>(ph[0]);
        std::uint16_t lenMinus1 = 0;
        std::memcpy(&lenMinus1, ph.data() + 2, 2);
        const std::uint64_t len = std::uint64_t{lenMinus1} + 1;
        if (len < 4 || len > left) throw IoError("damaged E57 file: bad packet length");
        if (type == 1) {
            const std::string pkt = pages.read(at, len);
            if (pkt.size() < 6) throw IoError("damaged E57 file: short data packet");
            std::uint16_t count = 0;
            std::memcpy(&count, pkt.data() + 4, 2);
            if (count != fieldCount) throw IoError("E57 data packet has " + std::to_string(count) + " streams, the prototype " + std::to_string(fieldCount));
            std::size_t pos = 6 + 2 * std::size_t{count};
            if (pos > pkt.size()) throw IoError("damaged E57 file: short data packet");
            for (std::size_t s = 0; s < count; ++s) {
                std::uint16_t bl = 0;
                std::memcpy(&bl, pkt.data() + 6 + 2 * s, 2);
                if (pos + bl > pkt.size()) throw IoError("damaged E57 file: stream past its packet");
                streams[s].append(pkt, pos, bl);
                pos += bl;
            }
        } else if (type != 0 && type != 2) {
            throw IoError("damaged E57 file: unknown packet type " + std::to_string(type));
        }
        at = pages.advance(at, len);
        left -= len;
    }
    return streams;
}

}  // namespace

TriMesh readE57(const std::string& bytes, E57Summary* summary) {
    const Pages pages(bytes);
    std::unique_ptr<xml::Element> root;
    try {
        root = xml::parse(pages.xml());
    } catch (const std::runtime_error& e) {
        throw IoError(std::string("E57 XML: ") + e.what());
    }
    E57Summary sum;
    TriMesh out;
    const xml::Element* data3D = root->child("data3D");
    if (!data3D) throw IoError("the E57 file has no 3D data");
    bool allNormals = true;
    for (const auto& scan : data3D->children) {
        const xml::Element* points = scan->child("points");
        if (!points) continue;
        const xml::Element* proto = points->child("prototype");
        if (!proto) throw IoError("E57 scan without a prototype");
        const auto records = static_cast<std::size_t>(intAttr(*points, "recordCount", 0));
        const auto offset = static_cast<std::uint64_t>(intAttr(*points, "fileOffset", 0));
        if (records > bytes.size()) throw IoError("damaged E57 file: more records than bytes");
        ++sum.scans;
        sum.records += records;
        if (records == 0) continue;

        std::vector<Field> fields;
        for (const auto& c : proto->children) fields.push_back(makeField(*c));
        const auto streams = bytestreams(pages, offset, fields.size());
        std::map<std::string, std::vector<double>> v;
        auto want = [](const std::string& n) {
            static const char* names[] = {"cartesianX", "cartesianY", "cartesianZ", "cartesianInvalidState", "sphericalRange", "sphericalAzimuth",
                                          "sphericalElevation", "sphericalInvalidState", "nor:normalX", "nor:normalY", "nor:normalZ"};
            for (const char* w : names)
                if (n == w) return true;
            return false;
        };
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (want(fields[i].name)) v[fields[i].name] = decode(fields[i], streams[i], records);

        const bool cart = v.count("cartesianX") && v.count("cartesianY") && v.count("cartesianZ");
        const bool sph = v.count("sphericalRange") && v.count("sphericalAzimuth") && v.count("sphericalElevation");
        if (!cart && !sph) throw IoError("E57 scan without Cartesian or spherical coordinates");
        const bool nor = v.count("nor:normalX") && v.count("nor:normalY") && v.count("nor:normalZ");
        allNormals = allNormals && nor;

        // Pose: rotation quaternion (w, x, y, z) then translation, both to the file's frame.
        Eigen::Quaterniond q = Eigen::Quaterniond::Identity();
        Vec3d t = Vec3d::Zero();
        if (const xml::Element* pose = scan->child("pose")) {
            if (const xml::Element* r = pose->child("rotation"))
                q = Eigen::Quaterniond(number(r->child("w")), number(r->child("x")), number(r->child("y")), number(r->child("z")));
            if (const xml::Element* tr = pose->child("translation")) t = Vec3d(number(tr->child("x")), number(tr->child("y")), number(tr->child("z")));
            if (q.norm() < 1e-9) throw IoError("E57 pose with a zero rotation");
            q.normalize();
        }
        const Eigen::Matrix3d R = q.toRotationMatrix();
        const std::vector<double>* invalid = cart ? (v.count("cartesianInvalidState") ? &v["cartesianInvalidState"] : nullptr)
                                                  : (v.count("sphericalInvalidState") ? &v["sphericalInvalidState"] : nullptr);
        for (std::size_t i = 0; i < records; ++i) {
            if (invalid && (*invalid)[i] != 0.0) { ++sum.invalid; continue; }
            Vec3d p;
            if (cart) {
                p = Vec3d(v["cartesianX"][i], v["cartesianY"][i], v["cartesianZ"][i]);
            } else {
                const double r = v["sphericalRange"][i], az = v["sphericalAzimuth"][i], el = v["sphericalElevation"][i];
                p = Vec3d(r * std::cos(el) * std::cos(az), r * std::cos(el) * std::sin(az), r * std::sin(el));
            }
            if (!p.allFinite()) { ++sum.invalid; continue; }
            out.vertices.push_back(((R * p + t) * 1000.0).cast<float>());  // metres -> mm
            if (nor) out.normals.push_back((R * Vec3d(v["nor:normalX"][i], v["nor:normalY"][i], v["nor:normalZ"][i])).normalized().cast<float>());
        }
    }
    if (!allNormals) out.normals.clear();
    if (out.vertices.empty()) throw IoError("the E57 file has no valid points");
    if (summary) *summary = sum;
    return out;
}

}  // namespace orv

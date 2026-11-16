// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/mesh/DeviationReport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace orv {

namespace {

using Rgb = std::array<float, 3>;

constexpr double kSheetW = 297.0, kSheetH = 210.0;  // A4 landscape, mm, y up

struct Item {
    enum Kind { Rect, Line, Text } kind{Rect};
    double x{0}, y{0}, w{0}, h{0};  // Rect: x y w h. Line: x y to x+w y+h. Text: x y, h = font height
    double width{0.2};              // line width
    Rgb colour{0, 0, 0};
    std::string text;
    int align{0};  // text: 0 left, 1 centre, 2 right
    bool bold{false};
};

std::string num(double v, int decimals = 3) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    return b;
}

std::string signedNum(double v, int decimals = 3) {
    const std::string s = num(v, decimals);
    return v > 0 && s != num(0, decimals) ? "+" + s : s;
}

// UTF-8 → the single bytes of WinAnsi (Latin-1 range); the rest becomes '?'.
std::string latin1(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) out += static_cast<char>(c);
        else if ((c == 0xC2 || c == 0xC3) && i + 1 < s.size()) {
            out += static_cast<char>(((c & 0x03) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F));
            ++i;
        } else if (c >= 0xC0) {
            out += '?';
            while (i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) ++i;
        }
    }
    return out;
}

std::string pdfEscape(const std::string& s) {
    std::string out;
    for (char ch : latin1(s)) {
        if (ch == '(' || ch == ')' || ch == '\\') out += '\\';
        out += ch;
    }
    return out;
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char ch : s) {
        if (ch == '&') out += "&amp;";
        else if (ch == '<') out += "&lt;";
        else if (ch == '>') out += "&gt;";
        else out += ch;
    }
    return out;
}

std::size_t utf8Length(const std::string& s) {
    std::size_t n = 0;
    for (char ch : s)
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++n;
    return n;
}

class Page {
public:
    void rect(double x, double y, double w, double h, Rgb c) {
        Item i;
        i.kind = Item::Rect;
        i.x = x; i.y = y; i.w = w; i.h = h;
        i.colour = c;
        items_.push_back(i);
    }
    void line(double x0, double y0, double x1, double y1, double width, Rgb c) {
        Item i;
        i.kind = Item::Line;
        i.x = x0; i.y = y0; i.w = x1 - x0; i.h = y1 - y0;
        i.width = width;
        i.colour = c;
        items_.push_back(i);
    }
    void text(double x, double y, double h, const std::string& t, int align = 0, bool bold = false, Rgb c = {0, 0, 0}) {
        Item i;
        i.kind = Item::Text;
        i.x = x; i.y = y; i.h = h;
        i.text = t;
        i.align = align;
        i.bold = bold;
        i.colour = c;
        items_.push_back(i);
    }
    const std::vector<Item>& items() const { return items_; }

private:
    std::vector<Item> items_;
};

void writeSvg(const Page& p, std::ostream& os) {
    auto col = [](Rgb c) {
        char b[16];
        std::snprintf(b, sizeof b, "#%02x%02x%02x", static_cast<int>(std::lround(255 * c[0])), static_cast<int>(std::lround(255 * c[1])), static_cast<int>(std::lround(255 * c[2])));
        return std::string(b);
    };
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << num(kSheetW, 0) << "mm\" height=\"" << num(kSheetH, 0)
       << "mm\" viewBox=\"0 0 " << num(kSheetW, 0) << " " << num(kSheetH, 0) << "\" font-family=\"Helvetica, Arial, sans-serif\">\n<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/>\n";
    for (const auto& i : p.items()) {
        if (i.kind == Item::Rect) {
            os << "<rect x=\"" << num(i.x) << "\" y=\"" << num(kSheetH - i.y - i.h) << "\" width=\"" << num(i.w) << "\" height=\"" << num(i.h) << "\" fill=\"" << col(i.colour) << "\"/>\n";
        } else if (i.kind == Item::Line) {
            os << "<line x1=\"" << num(i.x) << "\" y1=\"" << num(kSheetH - i.y) << "\" x2=\"" << num(i.x + i.w) << "\" y2=\"" << num(kSheetH - i.y - i.h) << "\" stroke=\"" << col(i.colour)
               << "\" stroke-width=\"" << num(i.width) << "\"/>\n";
        } else {
            os << "<text x=\"" << num(i.x) << "\" y=\"" << num(kSheetH - i.y) << "\" font-size=\"" << num(i.h) << "\" fill=\"" << col(i.colour) << "\" text-anchor=\""
               << (i.align == 2 ? "end" : i.align == 1 ? "middle" : "start") << "\"" << (i.bold ? " font-weight=\"bold\"" : "") << ">" << xmlEscape(i.text) << "</text>\n";
        }
    }
    os << "</svg>\n";
}

void writePdf(const Page& p, std::ostream& os) {
    constexpr double pt = 72.0 / 25.4;
    auto n = [](double v) { return num(v, 3); };
    std::ostringstream c;
    for (const auto& i : p.items()) {
        if (i.kind == Item::Rect) {
            c << n(i.colour[0]) << " " << n(i.colour[1]) << " " << n(i.colour[2]) << " rg " << n(i.x * pt) << " " << n(i.y * pt) << " " << n(i.w * pt) << " " << n(i.h * pt) << " re f\n";
        } else if (i.kind == Item::Line) {
            c << n(i.colour[0]) << " " << n(i.colour[1]) << " " << n(i.colour[2]) << " RG " << n(i.width * pt) << " w " << n(i.x * pt) << " " << n(i.y * pt) << " m " << n((i.x + i.w) * pt) << " "
              << n((i.y + i.h) * pt) << " l S\n";
        } else {
            const double width = static_cast<double>(utf8Length(i.text)) * 0.55 * i.h;
            const double x = i.x - (i.align == 2 ? width : i.align == 1 ? 0.5 * width : 0.0);
            c << n(i.colour[0]) << " " << n(i.colour[1]) << " " << n(i.colour[2]) << " rg BT " << (i.bold ? "/F2 " : "/F1 ") << n(i.h * pt) << " Tf " << n(x * pt) << " " << n(i.y * pt) << " Td ("
              << pdfEscape(i.text) << ") Tj ET\n";
        }
    }
    const std::string content = c.str();
    std::vector<std::size_t> offsets;
    std::ostringstream body;
    const std::string head = "%PDF-1.4\n";
    auto obj = [&](const std::string& s) {
        offsets.push_back(head.size() + static_cast<std::size_t>(body.tellp()));
        body << offsets.size() << " 0 obj\n" << s << "\nendobj\n";
    };
    obj("<< /Type /Catalog /Pages 2 0 R >>");
    obj("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + n(kSheetW * pt) + " " + n(kSheetH * pt) + "] /Contents 4 0 R /Resources << /Font << /F1 5 0 R /F2 6 0 R >> >> >>");
    obj("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream");
    obj("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    obj("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    os << head;
    const std::string b = body.str();
    os << b;
    const std::size_t xref = head.size() + b.size();
    os << "xref\n0 " << offsets.size() + 1 << "\n0000000000 65535 f \n";
    for (auto off : offsets) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off);
        os << buf;
    }
    os << "trailer\n<< /Size " << offsets.size() + 1 << " /Root 1 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";
}

const Rgb kInk{0.10f, 0.10f, 0.12f}, kGrey{0.45f, 0.45f, 0.48f}, kLight{0.85f, 0.85f, 0.87f};

}  // namespace

std::array<float, 3> deviationRgb(double dd, double tol, double range) {
    const float d = static_cast<float>(dd), tl = static_cast<float>(tol), rg = static_cast<float>(range);
    const float a = std::abs(d);
    if (a <= tl) return {0.30f, 0.75f, 0.35f};
    const float t = std::clamp((a - tl) / std::max(1e-6f, rg - tl), 0.f, 1.f);
    if (d > 0) return t < 0.5f ? Rgb{0.30f + 1.30f * t, 0.75f + 0.20f * t, 0.35f - 0.5f * t} : Rgb{0.95f, 0.85f - 1.4f * (t - 0.5f), 0.10f};
    return t < 0.5f ? Rgb{0.30f - 0.4f * t, 0.75f + 0.10f * t, 0.35f + 1.2f * t} : Rgb{0.10f, 0.80f - 1.2f * (t - 0.5f), 0.95f};
}

void writeDeviationReport(const DeviationReport& r, const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".pdf" && ext != ".svg") throw std::runtime_error("the report goes to .pdf or .svg");
    const bool es = r.spanish;
    auto L = [&](const char* en, const char* esp) { return std::string(es ? esp : en); };
    const double tol = r.tolerance, range = std::max(r.range, tol * 1.01);

    Page p;
    const double mx = 12;
    // Header.
    p.text(mx, 193, 7, r.title.empty() ? L("Deviation report", "Informe de desviación") : r.title, 0, true, kInk);
    p.text(mx, 186, 3.6, L("Model: ", "Modelo: ") + r.part + "   ·   " + L("Scan: ", "Escaneo: ") + r.scan, 0, false, kGrey);
    p.line(mx, 183, kSheetW - mx, 183, 0.3, kLight);

    // Summary.
    const DeviationStats& s = r.stats;
    double y = 175;
    p.text(mx, y, 4, L("Summary", "Resumen"), 0, true, kInk);
    y -= 6;
    struct Row { std::string k, v; };
    std::vector<Row> rows = {
        {L("Points measured", "Puntos medidos"), std::to_string(s.points)},
        {L("Within ±", "Dentro de ±") + num(tol, 3) + " mm", num(100.0 * s.withinFraction, 1) + " %"},
        {L("Median |distance|", "Mediana |distancia|"), num(s.median, 3) + " mm"},
        {L("90th percentile", "Percentil 90"), num(s.p90, 3) + " mm"},
        {L("95th percentile", "Percentil 95"), num(s.p95, 3) + " mm"},
        {L("Worst case", "Peor caso"), num(s.maxAbs, 3) + " mm"},
        {L("Mean (signed)", "Media (con signo)"), signedNum(s.mean, 3) + " mm"},
        {L("RMS", "RMS"), num(s.rms, 3) + " mm"},
    };
    if (r.texture > 0) rows.push_back({L("Texture discounted", "Textura descontada"), num(r.texture, 3) + " mm"});
    for (const auto& row : rows) {
        p.text(mx, y, 3.3, row.k, 0, false, kInk);
        p.text(mx + 78, y, 3.3, row.v, 2, true, kInk);
        y -= 4.2;
    }

    // Histogram of the signed distances.
    const double hx = mx, hw = 78, hy = 96, hh = 24;
    p.text(hx, hy + hh + 5, 4, L("Distribution of the distance", "Distribución de la distancia"), 0, true, kInk);
    constexpr int kBins = 41;
    std::vector<std::size_t> bin(kBins, 0);
    for (double d : r.distance) {
        const double t = (std::clamp(d, -range, range) + range) / (2 * range);
        ++bin[static_cast<std::size_t>(std::min(kBins - 1, static_cast<int>(t * kBins)))];
    }
    const std::size_t top = std::max<std::size_t>(1, *std::max_element(bin.begin(), bin.end()));
    for (int k = 0; k < kBins; ++k) {
        const double centre = -range + (k + 0.5) * 2 * range / kBins;
        const double bh = hh * static_cast<double>(bin[static_cast<std::size_t>(k)]) / static_cast<double>(top);
        p.rect(hx + k * hw / kBins, hy, hw / kBins * 0.92, bh, deviationRgb(centre, tol, range));
    }
    p.line(hx, hy, hx + hw, hy, 0.3, kInk);
    for (double t : {-tol, tol}) {
        const double x = hx + (t + range) / (2 * range) * hw;
        p.line(x, hy - 1, x, hy + hh, 0.2, kGrey);
    }
    p.text(hx, hy - 5, 3, signedNum(-range, 2), 0, false, kGrey);
    p.text(hx + hw / 2, hy - 5, 3, "0 mm", 1, false, kGrey);
    p.text(hx + hw, hy - 5, 3, signedNum(range, 2), 2, false, kGrey);
    p.text(hx, hy - 10, 2.8, L("scan inside the model", "escaneo dentro del modelo"), 0, false, kGrey);
    p.text(hx + hw, hy - 10, 2.8, L("scan outside the model", "escaneo fuera del modelo"), 2, false, kGrey);

    // The scan coloured, seen from three sides.
    Eigen::Vector3f lo = Eigen::Vector3f::Constant(1e30f), hi = Eigen::Vector3f::Constant(-1e30f);
    for (const auto& v : r.points) {
        lo = lo.cwiseMin(v);
        hi = hi.cwiseMax(v);
    }
    const double vx0 = 100, vx1 = kSheetW - mx, vy0 = 94, vy1 = 176;
    p.text(vx0, 175 + 0, 4, L("Scan coloured by its distance to the model", "Escaneo coloreado por su distancia al modelo"), 0, true, kInk);
    struct View { const char* name; int u, v, depth; bool flipU; };
    // top (x,y; looking down), front (x,z; looking along +y), right (y,z; looking along -x).
    const std::array<View, 3> views{{{"top", 0, 1, 2, false}, {"front", 0, 2, 1, false}, {"right", 1, 2, 0, false}}};
    const std::array<const char*, 3> viewEn{"Top", "Front", "Right"}, viewEs{"Superior", "Frontal", "Derecha"};
    if (!r.points.empty()) {
        const double cellW = (vx1 - vx0 - 2 * 6) / 3.0, cellH = vy1 - 8 - vy0;
        const Eigen::Vector3f size = (hi - lo).cwiseMax(1e-3f);
        // One scale for the three views, so their sizes agree.
        double scale = 1e30;
        for (const auto& v : views) scale = std::min(scale, std::min(cellW / size[v.u], cellH / size[v.v]));
        for (std::size_t vi = 0; vi < views.size(); ++vi) {
            const View& v = views[vi];
            const double ox = vx0 + static_cast<double>(vi) * (cellW + 6), oy = vy0;
            const double pw = size[v.u] * scale, ph = size[v.v] * scale;
            const double cx = ox + (cellW - pw) / 2, cy = oy + (cellH - ph) / 2;
            p.text(ox + cellW / 2, vy1 - 6, 3.4, es ? viewEs[vi] : viewEn[vi], 1, false, kGrey);
            // Far points first, so the near ones cover them.
            std::vector<std::size_t> order;
            const std::size_t stride = std::max<std::size_t>(1, r.points.size() / 24000);
            for (std::size_t i = 0; i < r.points.size(); i += stride) order.push_back(i);
            const double sign = 1.0;
            std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return sign * r.points[a][v.depth] < sign * r.points[b][v.depth]; });
            if (vi == 1) std::reverse(order.begin(), order.end());  // front looks along +y: near is small y
            const double dot = std::clamp(1.9 * std::sqrt(pw * ph / static_cast<double>(std::max<std::size_t>(1, order.size()))), 0.45, 1.8);
            for (std::size_t i : order) {
                const double x = cx + (r.points[i][v.u] - lo[v.u]) * scale, yy = cy + (r.points[i][v.v] - lo[v.v]) * scale;
                p.rect(x - dot / 2, yy - dot / 2, dot, dot, deviationRgb(r.distance[i], tol, range));
            }
        }
    }
    // Colour legend.
    {
        const double lx = vx0, ly = 98, lw = vx1 - vx0 - 60;
        constexpr int kSteps = 60;
        for (int k = 0; k < kSteps; ++k) p.rect(lx + k * lw / kSteps, ly, lw / kSteps + 0.1, 3, deviationRgb(-range + (k + 0.5) * 2 * range / kSteps, tol, range));
        p.text(lx, ly - 3.5, 2.8, signedNum(-range, 2) + " mm", 0, false, kGrey);
        p.text(lx + lw / 2, ly - 3.5, 2.8, L("within ±", "dentro de ±") + num(tol, 2) + " mm", 1, false, kGrey);
        p.text(lx + lw, ly - 3.5, 2.8, signedNum(range, 2) + " mm", 2, false, kGrey);
    }

    // Worst faces and zones.
    p.line(mx, 80, kSheetW - mx, 80, 0.3, kLight);
    double fy = 74;
    p.text(mx, fy, 4, L("Face by face (largest systematic offset first)", "Cara por cara (mayor desvío sistemático primero)"), 0, true, kInk);
    fy -= 6;
    const double cols[6] = {mx, mx + 40, mx + 62, mx + 82, mx + 102, mx + 122};
    const char* headEn[6] = {"Face", "Points", "Mean", "Scatter", "95 %", "Worst"};
    const char* headEs[6] = {"Cara", "Puntos", "Media", "Dispersión", "95 %", "Peor"};
    for (int k = 0; k < 6; ++k) p.text(cols[k] + (k ? 18 : 0), fy, 3, es ? headEs[k] : headEn[k], k ? 2 : 0, true, kGrey);
    fy -= 1.5;
    p.line(mx, fy, mx + 140, fy, 0.2, kLight);
    fy -= 4;
    for (std::size_t i = 0; i < r.faces.size() && i < 11; ++i) {
        const ReportFace& f = r.faces[i];
        const Rgb dot = deviationRgb(f.mean, tol, range);
        p.rect(mx - 4, fy - 0.3, 2.2, 2.2, dot);
        p.text(cols[0], fy, 3, f.label, 0, false, kInk);
        p.text(cols[1] + 18, fy, 3, std::to_string(f.points), 2, false, kInk);
        p.text(cols[2] + 18, fy, 3, signedNum(f.mean, 3), 2, false, kInk);
        p.text(cols[3] + 18, fy, 3, num(f.scatter, 3), 2, false, kInk);
        p.text(cols[4] + 18, fy, 3, num(f.p95, 3), 2, false, kInk);
        p.text(cols[5] + 18, fy, 3, num(f.worst, 3), 2, false, kInk);
        fy -= 4.6;
    }
    if (r.faces.empty()) p.text(mx, fy, 3, L("(no face has enough points)", "(ninguna cara tiene puntos suficientes)"), 0, false, kGrey);

    const double zx = 170;
    double zy = 74;
    p.text(zx, zy, 4, L("Zones outside the tolerance", "Zonas fuera de la tolerancia"), 0, true, kInk);
    zy -= 6;
    if (r.zones.empty()) p.text(zx, zy, 3, L("None: the whole scan is within the tolerance.", "Ninguna: todo el escaneo está dentro de la tolerancia."), 0, false, kGrey);
    for (std::size_t i = 0; i < r.zones.size() && i < 8; ++i) {
        const ReportZone& z = r.zones[i];
        p.rect(zx - 4, zy - 0.3, 2.2, 2.2, deviationRgb(z.meanDistance, tol, range));
        p.text(zx, zy, 3, num(z.percent, 1) + " %  " + signedNum(z.meanDistance, 3) + " mm  " + L("at", "en") + " (" + num(z.centre[0], 1) + ", " + num(z.centre[1], 1) + ", " + num(z.centre[2], 1) + ")", 0, false, kInk);
        zy -= 4;
        p.text(zx + 3, zy, 2.7, L("size ", "tamaño ") + num(z.size[0], 1) + " × " + num(z.size[1], 1) + " × " + num(z.size[2], 1) + " mm  ·  " +
                                      (z.meanDistance > 0 ? L("material missing in the model", "falta material en el modelo") : L("too much material in the model", "sobra material en el modelo")),
               0, false, kGrey);
        zy -= 5;
    }
    p.text(mx, 8, 2.8, L("Distances in mm, positive where the scan lies outside the model. Made with OpenRevers.", "Distancias en mm, positivas donde el escaneo queda fuera del modelo. Hecho con OpenRevers."), 0, false, kGrey);

    std::ofstream os(path, std::ios::binary);
    if (!os) throw std::runtime_error("cannot write " + path.string());
    if (ext == ".svg") writeSvg(p, os);
    else writePdf(p, os);
    if (!os) throw std::runtime_error("cannot write " + path.string());
}

}  // namespace orv

// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/Xml.h"

#include <cctype>
#include <stdexcept>

namespace orv::xml {
namespace {

constexpr int kMaxDepth = 256;

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x110000) { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else throw std::runtime_error("XML: bad character reference");
}

/// Character data with entity references resolved.
std::string decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        const auto semi = s.find(';', i);
        if (semi == std::string_view::npos) throw std::runtime_error("XML: unterminated entity");
        const std::string_view e = s.substr(i + 1, semi - i - 1);
        if (e == "lt") out += '<';
        else if (e == "gt") out += '>';
        else if (e == "amp") out += '&';
        else if (e == "quot") out += '"';
        else if (e == "apos") out += '\'';
        else if (e.size() > 1 && e[0] == '#') {
            const bool hex = e[1] == 'x' || e[1] == 'X';
            const std::string digits(e.substr(hex ? 2 : 1));
            if (digits.empty() || digits.size() > 8) throw std::runtime_error("XML: bad character reference");
            appendUtf8(out, std::stoul(digits, nullptr, hex ? 16 : 10));
        } else {
            throw std::runtime_error("XML: unknown entity &" + std::string(e) + ";");
        }
        i = semi;
    }
    return out;
}

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    std::unique_ptr<Element> document() {
        skipProlog();
        if (!peek('<')) throw std::runtime_error("XML: no root element");
        auto root = element(0);
        skipMisc();
        if (pos_ < s_.size()) throw std::runtime_error("XML: content after the root element");
        return root;
    }

private:
    bool peek(char c) const { return pos_ < s_.size() && s_[pos_] == c; }
    bool startsWith(std::string_view p) const { return s_.substr(pos_, p.size()) == p; }
    void skipSpace() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    void skipPast(std::string_view end) {
        const auto e = s_.find(end, pos_);
        if (e == std::string_view::npos) throw std::runtime_error("XML: unterminated construct");
        pos_ = e + end.size();
    }
    void skipMisc() {
        while (true) {
            skipSpace();
            if (startsWith("<?")) skipPast("?>");
            else if (startsWith("<!--")) skipPast("-->");
            else return;
        }
    }
    void skipProlog() {
        if (s_.substr(0, 3) == "\xEF\xBB\xBF") pos_ = 3;  // UTF-8 byte order mark
        skipMisc();
        if (startsWith("<!DOCTYPE")) throw std::runtime_error("XML: document type declarations are not accepted");
    }
    std::string_view name() {
        const auto start = pos_;
        while (pos_ < s_.size() && !std::isspace(static_cast<unsigned char>(s_[pos_])) && s_[pos_] != '>' && s_[pos_] != '/' && s_[pos_] != '=') ++pos_;
        if (pos_ == start) throw std::runtime_error("XML: expected a name");
        return s_.substr(start, pos_ - start);
    }

    std::unique_ptr<Element> element(int depth) {
        if (depth > kMaxDepth) throw std::runtime_error("XML: elements nest too deep");
        ++pos_;  // '<'
        auto el = std::make_unique<Element>();
        el->name = std::string(name());
        while (true) {
            skipSpace();
            if (pos_ >= s_.size()) throw std::runtime_error("XML: unterminated tag");
            if (startsWith("/>")) { pos_ += 2; return el; }
            if (peek('>')) { ++pos_; break; }
            const std::string key(name());
            skipSpace();
            if (!peek('=')) throw std::runtime_error("XML: expected = after " + key);
            ++pos_;
            skipSpace();
            if (!peek('"') && !peek('\'')) throw std::runtime_error("XML: expected a quoted value for " + key);
            const char q = s_[pos_++];
            const auto end = s_.find(q, pos_);
            if (end == std::string_view::npos) throw std::runtime_error("XML: unterminated value");
            el->attributes.emplace_back(key, decode(s_.substr(pos_, end - pos_)));
            pos_ = end + 1;
        }
        // Content.
        while (true) {
            if (pos_ >= s_.size()) throw std::runtime_error("XML: element " + el->name + " is not closed");
            if (startsWith("</")) {
                pos_ += 2;
                const std::string_view n = name();
                if (n != el->name) throw std::runtime_error("XML: </" + std::string(n) + "> closes <" + el->name + ">");
                skipSpace();
                if (!peek('>')) throw std::runtime_error("XML: bad closing tag");
                ++pos_;
                return el;
            }
            if (startsWith("<![CDATA[")) {
                pos_ += 9;
                const auto e = s_.find("]]>", pos_);
                if (e == std::string_view::npos) throw std::runtime_error("XML: unterminated CDATA");
                el->text.append(s_.substr(pos_, e - pos_));
                pos_ = e + 3;
            } else if (startsWith("<!--")) {
                skipPast("-->");
            } else if (startsWith("<?")) {
                skipPast("?>");
            } else if (startsWith("<!")) {
                throw std::runtime_error("XML: declarations are not accepted");
            } else if (peek('<')) {
                el->children.push_back(element(depth + 1));
            } else {
                const auto e = s_.find('<', pos_);
                const auto end = e == std::string_view::npos ? s_.size() : e;
                el->text += decode(s_.substr(pos_, end - pos_));
                pos_ = end;
            }
        }
    }

    std::string_view s_;
    std::size_t pos_{0};
};

}  // namespace

std::unique_ptr<Element> parse(std::string_view text) { return Parser(text).document(); }

}  // namespace orv::xml

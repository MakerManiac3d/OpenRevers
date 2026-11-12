// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands/FieldKind.h"

#include <map>
#include <string>

namespace orv {

FieldKind fieldKind(const json& s) {
    static const std::map<std::string, FieldKind> byKind = {
        {"body", FieldKind::Body},           {"body-list", FieldKind::BodyList}, {"feature", FieldKind::Feature},
        {"sketch", FieldKind::Sketch},       {"sketch-list", FieldKind::SketchList}, {"operation", FieldKind::Operation},
        {"surface", FieldKind::Surface},     {"part-item", FieldKind::PartItem}, {"parameter", FieldKind::Parameter},
        {"open-path", FieldKind::OpenPath},  {"save-path", FieldKind::SavePath}, {"folder", FieldKind::Folder},
        {"point", FieldKind::Point},         {"point2", FieldKind::Point2},     {"point-list", FieldKind::PointList},
        {"plane", FieldKind::Plane},         {"mesh-faces", FieldKind::MeshFaces}, {"index-list", FieldKind::IndexList},
        {"loops", FieldKind::Loops},
    };
    if (s.contains("x-kind") && s["x-kind"].is_string()) {
        const auto it = byKind.find(s["x-kind"].get<std::string>());
        if (it != byKind.end()) return it->second;
    }
    if (!s.contains("type")) return FieldKind::Unsupported;
    const json& t = s["type"];
    if (t.is_array()) return FieldKind::Value;  // ["number", "string"]
    const std::string type = t.get<std::string>();
    if (type == "string") return s.contains("enum") ? FieldKind::Choice : FieldKind::Text;
    if (type == "number") return FieldKind::Number;
    if (type == "integer") return FieldKind::Integer;
    if (type == "boolean") return FieldKind::Boolean;
    return FieldKind::Unsupported;  // a list or an object with no control of its own
}

const char* fieldKindName(FieldKind k) {
    switch (k) {
        case FieldKind::Body: return "body";
        case FieldKind::BodyList: return "body-list";
        case FieldKind::Feature: return "feature";
        case FieldKind::Sketch: return "sketch";
        case FieldKind::SketchList: return "sketch-list";
        case FieldKind::Operation: return "operation";
        case FieldKind::Surface: return "surface";
        case FieldKind::PartItem: return "part-item";
        case FieldKind::Parameter: return "parameter";
        case FieldKind::OpenPath: return "open-path";
        case FieldKind::SavePath: return "save-path";
        case FieldKind::Folder: return "folder";
        case FieldKind::Choice: return "choice";
        case FieldKind::Text: return "text";
        case FieldKind::Number: return "number";
        case FieldKind::Integer: return "integer";
        case FieldKind::Boolean: return "boolean";
        case FieldKind::Value: return "value";
        case FieldKind::Point: return "point";
        case FieldKind::Point2: return "point2";
        case FieldKind::PointList: return "point-list";
        case FieldKind::Plane: return "plane";
        case FieldKind::MeshFaces: return "mesh-faces";
        case FieldKind::IndexList: return "index-list";
        case FieldKind::Loops: return "loops";
        case FieldKind::Unsupported: return "unsupported";
    }
    return "unsupported";
}

}  // namespace orv

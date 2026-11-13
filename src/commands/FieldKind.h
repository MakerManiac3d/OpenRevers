// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — what kind of input a field of a command's schema is, and so
// which control the interface gives it. Every field must map to a control a
// person can fill by clicking or choosing: the parity test fails on any
// field left as raw JSON, so what an agent can send, a person can give too.
#pragma once

#include <nlohmann/json.hpp>

namespace orv {

using json = nlohmann::json;

enum class FieldKind {
    Body,         // a body of the document
    BodyList,     // several bodies, ticked
    Feature,      // a shape of a body's segmentation
    Sketch,       // a sketch of a part
    SketchList,   // sketches in order
    Operation,    // an operation of a part
    Surface,      // a free-form surface of a part
    PartItem,     // a sketch, operation or surface
    Parameter,    // a parameter of a part
    OpenPath,
    SavePath,
    Folder,
    Choice,       // one of a list
    Text,         // free words (a name, a topic)
    Number,
    Integer,
    Boolean,
    Value,        // a number or a parameter's name
    Point,        // [x, y, z], from a click in the view
    Point2,       // [x, y] in a sketch
    PointList,    // [[x, y, z], ...], clicks in the view in order
    Plane,        // {origin, normal, xDir}, from a face clicked in the view
    MeshFaces,    // face (or point) indices: the lasso selection
    IndexList,    // small integers typed as 0, 2, 5
    Loops,        // a sketch's loops: drawn in the sketch editor
    Constraints,  // a sketch's constraints: set in the sketch editor
    Unsupported,  // raw JSON: not allowed
};

FieldKind fieldKind(const json& property);
const char* fieldKindName(FieldKind k);

}  // namespace orv

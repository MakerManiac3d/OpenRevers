// SPDX-License-Identifier: GPL-3.0-or-later
// Parity: everything an agent can send a command, a person can give in the
// interface by clicking or choosing. A field the interface could only take
// as typed JSON (or a reference typed by hand) fails this test.
#include "commands/Command.h"
#include "commands/FieldKind.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace orv;

TEST(Parity, EveryFieldOfEveryCommandHasAControl) {
    CommandRegistry reg;
    registerCoreCommands(reg);
    // Fields that name something of the document must not be free text.
    const std::set<std::string> references = {"sketch", "operation", "surface", "item", "shape", "sections", "reference", "scan", "bodies", "faces"};
    std::size_t fields = 0;
    for (const Command* c : reg.all()) {
        const json& props = c->schema.value("properties", json::object());
        for (auto it = props.begin(); it != props.end(); ++it) {
            ++fields;
            const FieldKind k = fieldKind(it.value());
            EXPECT_NE(k, FieldKind::Unsupported) << c->name << "." << it.key() << " has no control: give it an x-kind";
            if (references.count(it.key())) EXPECT_NE(k, FieldKind::Text) << c->name << "." << it.key() << " is a reference typed by hand";
            // A field said to belong to another field's body must name a real field.
            if (it.value().contains("x-of")) EXPECT_TRUE(props.contains(it.value()["x-of"].get<std::string>())) << c->name << "." << it.key();
        }
    }
    EXPECT_GT(fields, 250u);
}

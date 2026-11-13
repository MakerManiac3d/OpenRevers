// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the tool icons, drawn in code (no image files): simple line
// glyphs in one style, so the ribbon reads as one family.
#pragma once

#include <QIcon>
#include <QString>

namespace orv {

/// The icon for a command (mesh.smooth) or a ribbon action (lasso,
/// edit_sketch...). Unknown names get a rounded tile with their initials.
QIcon toolIcon(const QString& name);

}  // namespace orv

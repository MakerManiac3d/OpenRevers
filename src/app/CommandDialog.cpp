// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/CommandDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <cstdint>

namespace orv {

CommandDialog::CommandDialog(const Command& command, const std::vector<Body>& bodies, const QString& selectedBody, const QString& lang, QWidget* parent)
    : QDialog(parent), command_(command) {
    const std::string l = lang.toStdString();
    setWindowTitle(QString::fromStdString(command.title.in(l)));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(QString::fromStdString(command.description.in(l)));
    intro->setWordWrap(true);
    intro->setMaximumWidth(520);
    layout->addWidget(intro);
    auto* form = new QFormLayout();
    layout->addLayout(form);

    const json& props = command.schema.value("properties", json::object());
    std::vector<std::string> required = command.schema.value("required", std::vector<std::string>{});
    for (auto it = props.begin(); it != props.end(); ++it) {
        const std::string key = it.key();
        const json& s = it.value();
        const std::string type = s.value("type", "");
        const std::string kind = s.value("x-kind", "");
        QString label = QString::fromStdString(l == "es" && s.contains("x-es") ? s["x-es"].get<std::string>() : s.value("description", key));
        if (s.contains("x-unit")) label += QString(" (%1)").arg(QString::fromStdString(s["x-unit"]));
        const bool isRequired = std::find(required.begin(), required.end(), key) != required.end();
        QWidget* field = nullptr;

        if (kind == "body") {
            auto* box = new QComboBox();
            for (const auto& b : bodies) box->addItem(QString::fromStdString(b.name + "  (" + b.ref() + ")"), QString::fromStdString(b.ref()));
            const int i = box->findData(selectedBody);
            if (i >= 0) box->setCurrentIndex(i);
            readers_[key] = [box]() { return box->currentData().isValid() ? json(box->currentData().toString().toStdString()) : json(nullptr); };
            field = box;
        } else if (kind == "open-path" || kind == "save-path") {
            auto* row = new QWidget();
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            auto* edit = new QLineEdit();
            auto* browse = new QToolButton();
            browse->setText("…");
            h->addWidget(edit, 1);
            h->addWidget(browse);
            const QString filter = QString::fromStdString(s.value("x-filter", ""));
            const bool save = kind == "save-path";
            connect(browse, &QToolButton::clicked, this, [this, edit, filter, save, label]() {
                const QString f = save ? QFileDialog::getSaveFileName(this, label, edit->text(), filter) : QFileDialog::getOpenFileName(this, label, edit->text(), filter);
                if (!f.isEmpty()) edit->setText(f);
            });
            readers_[key] = [edit]() { return edit->text().isEmpty() ? json(nullptr) : json(edit->text().toStdString()); };
            field = row;
        } else if (type == "string" && s.contains("enum")) {
            auto* box = new QComboBox();
            for (const auto& e : s["enum"]) box->addItem(QString::fromStdString(e.get<std::string>()));
            box->setCurrentText(QString::fromStdString(s.value("default", "")));
            readers_[key] = [box]() { return json(box->currentText().toStdString()); };
            field = box;
        } else if (type == "string") {
            auto* edit = new QLineEdit();
            readers_[key] = [edit]() { return edit->text().isEmpty() ? json(nullptr) : json(edit->text().toStdString()); };
            field = edit;
        } else if (type == "number") {
            auto* spin = new QDoubleSpinBox();
            const double mn = s.value("minimum", -1e9), mx = s.value("maximum", 1e9), def = s.value("default", 0.0);
            spin->setRange(mn, mx);
            const double mag = std::max({std::abs(def), std::abs(mn), 1e-6});
            spin->setDecimals(mag < 0.01 ? 6 : 4);
            spin->setSingleStep(def > 0 ? def / 10.0 : (mx - mn) / 1000.0);
            spin->setValue(def);
            readers_[key] = [spin]() { return json(spin->value()); };
            field = spin;
        } else if (type == "integer") {
            auto* spin = new QSpinBox();
            spin->setRange(static_cast<int>(std::max<long long>(s.value("minimum", 0LL), INT32_MIN)), static_cast<int>(std::min<long long>(s.value("maximum", 1000000LL), INT32_MAX)));
            spin->setValue(static_cast<int>(s.value("default", 0LL)));
            readers_[key] = [spin]() { return json(spin->value()); };
            field = spin;
        } else if (type == "boolean") {
            auto* check = new QCheckBox();
            check->setChecked(s.value("default", false));
            readers_[key] = [check]() { return json(check->isChecked()); };
            field = check;
        }
        if (!field) continue;
        field->setToolTip(QString::fromStdString(key));
        form->addRow(label + (isRequired ? " *" : ""), field);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Run"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

json CommandDialog::values() const {
    json out = json::object();
    for (const auto& [k, read] : readers_) {
        json v = read();
        if (!v.is_null()) out[k] = v;
    }
    return out;
}

}  // namespace orv

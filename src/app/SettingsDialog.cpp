// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/SettingsDialog.h"

#include "core/util/Config.h"

#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace orv {

SettingsDialog::SettingsDialog(const QString&, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    auto* layout = new QVBoxLayout(this);

    auto* general = new QGroupBox(tr("General"));
    auto* gf = new QFormLayout(general);
    language_ = new QComboBox();
    language_->addItem(tr("System language"), "auto");
    language_->addItem("Español", "es");
    language_->addItem("English", "en");
    QSettings s;
    language_->setCurrentIndex(std::max(0, language_->findData(s.value("language", "auto"))));
    gf->addRow(tr("Language"), language_);
    layout->addWidget(general);

    const AgentSettings a = loadAgentSettings();
    auto* agent = new QGroupBox(tr("AI agent (MCP)"));
    auto* af = new QVBoxLayout(agent);
    auto* note = new QLabel(tr("An AI agent such as Claude can drive OpenRevers through the openrevers-cli bridge. It connects on 127.0.0.1 only, "
                               "with a pairing token. What the agent reads goes to its AI model; nothing else leaves this computer."));
    note->setWordWrap(true);
    af->addWidget(note);
    agentEnabled_ = new QCheckBox(tr("Let an AI agent connect"));
    agentEnabled_->setChecked(a.enabled);
    af->addWidget(agentEnabled_);
    auto* pf = new QFormLayout();
    port_ = new QSpinBox();
    port_->setRange(1024, 65535);
    port_->setValue(a.port);
    pf->addRow(tr("Local port"), port_);
    af->addLayout(pf);
    af->addWidget(new QLabel(tr("Folders the agent may read and write (empty: your home folder):")));
    folders_ = new QListWidget();
    for (const auto& f : a.allowedFolders) folders_->addItem(QString::fromStdString(f.string()));
    af->addWidget(folders_);
    auto* row = new QHBoxLayout();
    auto* addBtn = new QPushButton(tr("Add folder…"));
    auto* removeBtn = new QPushButton(tr("Remove"));
    row->addWidget(addBtn);
    row->addWidget(removeBtn);
    row->addStretch(1);
    af->addLayout(row);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Folder the agent may use"));
        if (!d.isEmpty()) folders_->addItem(QDir::toNativeSeparators(d));
    });
    connect(removeBtn, &QPushButton::clicked, this, [this]() { delete folders_->currentItem(); });
    layout->addWidget(agent);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(560, 520);
}

void SettingsDialog::save() {
    QSettings s;
    const QString lang = language_->currentData().toString();
    languageChanged_ = lang != s.value("language", "auto").toString();
    s.setValue("language", lang);
    AgentSettings a = loadAgentSettings();
    a.enabled = agentEnabled_->isChecked();
    a.port = port_->value();
    a.allowedFolders.clear();
    for (int i = 0; i < folders_->count(); ++i) a.allowedFolders.emplace_back(folders_->item(i)->text().toStdString());
    try {
        saveAgentSettings(a);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Settings"), QString::fromUtf8(e.what()));
    }
}

}  // namespace orv

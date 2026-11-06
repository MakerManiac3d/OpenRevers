// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — settings: language, and what an AI agent may do.
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QListWidget;
class QSpinBox;

namespace orv {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const QString& currentLang, QWidget* parent = nullptr);
    bool languageChanged() const { return languageChanged_; }

private:
    void save();

    QComboBox* language_{nullptr};
    QCheckBox* agentEnabled_{nullptr};
    QSpinBox* port_{nullptr};
    QListWidget* folders_{nullptr};
    bool languageChanged_{false};
};

}  // namespace orv

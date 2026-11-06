// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — the desktop application.
#include "app/MainWindow.h"
#include "core/util/Version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStyleFactory>
#include <QTranslator>

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    QApplication::setOrganizationName("OpenRevers");
    QApplication::setApplicationName("OpenRevers");
    QApplication::setApplicationVersion(orv::kVersionString);
    QApplication::setWindowIcon(QIcon(":/openrevers.svg"));
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QSettings settings;
    QString lang = settings.value("language", "auto").toString();
    if (lang == "auto") lang = QLocale::system().language() == QLocale::Spanish ? "es" : "en";

    QTranslator appTr, qtTr;
    if (lang == "es") {
        if (appTr.load(":/i18n/openrevers_es.qm")) QApplication::installTranslator(&appTr);
        if (qtTr.load("qtbase_es", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) QApplication::installTranslator(&qtTr);
        QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("OpenRevers");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("files", "Meshes (.stl .obj .ply .3mf) or a project (.orvp) to open");
    parser.process(app);

    orv::MainWindow w(lang);
    w.show();
    if (!parser.positionalArguments().isEmpty()) w.openPaths(parser.positionalArguments());
    return QApplication::exec();
}

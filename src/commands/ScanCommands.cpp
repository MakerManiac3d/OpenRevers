// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — scan.*: scanning straight into OpenRevers with a scanner of
// CrealityScan (through the library CrealityScan installed) or the simulator.
#include "commands/ScanCommands.h"

#include "scan/ScanBackend.h"

#include <cmath>

namespace orv {

namespace {

using namespace schema;

json statusJson(const ScanStatus& s) {
    return {{"backend", s.backend},
            {"libraryFound", s.libraryFound},
            {"library", s.library},
            {"version", s.version},
            {"error", s.error.empty() ? json(nullptr) : json(s.error)},
            {"state", scanStateName(s.state)},
            {"device", s.device},
            {"points", s.points},
            {"progress", std::round(s.progress * 1000) / 1000},
            {"lastStatus", s.lastStatus}};
}

/// Run `f` on the scanner, turning its errors into command errors.
template <class F>
auto withScanner(CommandContext& ctx, F&& f) {
    auto& mgr = ScanManager::instance();
    auto lock = mgr.lock();
    try {
        return f(mgr.backend());
    } catch (const ScanError& e) {
        throw CommandError(e.code, ctx.say("Scanner: ", "Escáner: ") + e.what());
    } catch (const std::exception& e) {
        throw CommandError("failed", ctx.say("Scanner: ", "Escáner: ") + e.what());
    }
}

json stateAfter(CommandContext& ctx, ScanBackend& b, const std::string& en, const std::string& es) {
    json out = statusJson(b.status());
    out["summary"] = ctx.say(en, es);
    return out;
}

}  // namespace

void registerScanCommands(CommandRegistry& R) {
    R.add({"scan.status", "scan", {"Scanner status", "Estado del escáner"},
           {"Whether the scanning library of CrealityScan was found (and its version), the scanner's state and how many points the scan has. OpenRevers does not ship that library: it uses the one CrealityScan installed (Windows). ORV_SCAN_SIMULATOR=1 or scan.use_simulator gives a pretend scanner for trying things out.",
            "Si se ha encontrado la biblioteca de escaneo de CrealityScan (y su versión), el estado del escáner y cuántos puntos lleva el escaneo. OpenRevers no incluye esa biblioteca: usa la que instaló CrealityScan (Windows). ORV_SCAN_SIMULATOR=1 o scan.use_simulator dan un escáner simulado para probar."},
           object({}), false, false,
           [](CommandContext& ctx, const json&) {
               return withScanner(ctx, [&](ScanBackend& b) {
                   ScanStatus s = b.status();
                   json out = statusJson(s);
                   out["summary"] = s.libraryFound
                       ? ctx.say("Scanner library ready (" + s.backend + "), state: " + scanStateName(s.state) + ".",
                                 "Biblioteca de escaneo lista (" + s.backend + "), estado: " + scanStateName(s.state) + ".")
                       : ctx.say("No scanner library: " + s.error, "Sin biblioteca de escaneo: " + s.error);
                   return out;
               });
           }});

    R.add({"scan.use_simulator", "scan", {"Use the scan simulator", "Usar el simulador de escaneo"},
           {"Switch between the simulated scanner (on: true) and the library of CrealityScan (on: false; library_dir chooses its folder, by default CrealityScan's Plugins\\x86_64). Disconnects the scanner in use.",
            "Cambia entre el escáner simulado (on: true) y la biblioteca de CrealityScan (on: false; library_dir elige su carpeta, por defecto Plugins\\x86_64 de CrealityScan). Desconecta el escáner en uso."},
           object({{"on", boolean({"Use the simulator", "Usar el simulador"}, true)},
                   {"library_dir", string({"Folder of lib_orbbec_scan.dll", "Carpeta de lib_orbbec_scan.dll"})}}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               auto& mgr = ScanManager::instance();
               auto lock = mgr.lock();
               const bool on = in["on"].get<bool>();
               mgr.use(on ? "simulator" : "obscan", in.contains("library_dir") ? std::filesystem::path(in["library_dir"].get<std::string>()) : std::filesystem::path());
               return stateAfter(ctx, mgr.backend(), on ? "Using the simulated scanner." : "Using the CrealityScan library.",
                                 on ? "Usando el escáner simulado." : "Usando la biblioteca de CrealityScan.");
           }});

    R.add({"scan.devices", "scan", {"List scanners", "Listar escáneres"},
           {"The scanners plugged in, with their index for scan.connect.", "Los escáneres conectados, con su índice para scan.connect."},
           object({}), false, false,
           [](CommandContext& ctx, const json&) {
               return withScanner(ctx, [&](ScanBackend& b) {
                   json list = json::array();
                   for (const auto& d : b.devices())
                       list.push_back({{"index", d.index}, {"pid", d.pid}, {"name", d.name}, {"serial", d.serial}});
                   const auto n = std::to_string(list.size());
                   return json{{"devices", list}, {"summary", ctx.say(n + " scanner(s) found.", n + " escáner(es) encontrados.")}};
               });
           }});

    R.add({"scan.connect", "scan", {"Connect scanner", "Conectar escáner"},
           {"Open a scanner and get it ready to scan with these settings: what is scanned (mode), accuracy, the object's surface and the light. Then scan.preview, scan.start, scan.stop.",
            "Abre un escáner y lo prepara con estos ajustes: qué se escanea (modo), precisión, la superficie del objeto y la luz. Después scan.preview, scan.start, scan.stop."},
           object({{"device", integer({"Scanner index (-1: the active one)", "Índice del escáner (-1: el activo)"}, -1, -1, 16)},
                   {"mode", choice({"What is scanned", "Qué se escanea"}, {"object_small", "object_medium", "object_big", "face", "human"}, "object_small")},
                   {"accuracy", choice({"Accuracy", "Precisión"}, {"high", "fast"}, "high")},
                   {"material", choice({"The object's surface", "Superficie del objeto"}, {"standard", "black", "white", "reflective"}, "standard")},
                   {"light", choice({"Light: infrared or blue laser lines", "Luz: infrarroja o líneas láser azules"}, {"infrared", "blue_laser"}, "infrared")},
                   {"voxel_size", number({"Point spacing (0: the scanner's default)", "Separación de puntos (0: la del escáner)"}, 0, 0, 10, "mm")}}),
           false, false,
           [](CommandContext& ctx, const json& in) {
               return withScanner(ctx, [&](ScanBackend& b) {
                   ScanSettings st;
                   st.mode = in["mode"];
                   st.accuracy = in["accuracy"];
                   st.material = in["material"];
                   st.light = in["light"];
                   st.voxelSize = in["voxel_size"];
                   b.connect(in["device"].get<int>(), st);
                   return stateAfter(ctx, b, "Scanner connected. Next: scan.preview, then scan.start.",
                                     "Escáner conectado. Siguiente: scan.preview y luego scan.start.");
               });
           }});

    auto simple = [&R](const char* name, Text title, Text desc, void (ScanBackend::*fn)(), const char* en, const char* es) {
        R.add({name, "scan", std::move(title), std::move(desc), object({}), false, false,
               [fn, en, es](CommandContext& ctx, const json&) {
                   return withScanner(ctx, [&](ScanBackend& b) {
                       (b.*fn)();
                       return stateAfter(ctx, b, en, es);
                   });
               }});
    };
    simple("scan.preview", {"Preview", "Vista previa"},
           {"Start the scanner's stream without recording, to aim it.", "Arranca la imagen del escáner sin grabar, para apuntar."},
           &ScanBackend::preview, "Previewing.", "En vista previa.");
    simple("scan.start", {"Start scanning", "Empezar a escanear"},
           {"Record: the points seen are added to the scan. The view shows them as they come.", "Graba: los puntos vistos se suman al escaneo. La vista los muestra según llegan."},
           &ScanBackend::start, "Scanning.", "Escaneando.");
    simple("scan.pause", {"Pause scanning", "Pausar el escaneo"},
           {"Stop adding points for now (scan.start goes on).", "Deja de sumar puntos por ahora (scan.start continúa)."},
           &ScanBackend::pause, "Paused.", "En pausa.");
    simple("scan.stop", {"Stop scanning", "Terminar el escaneo"},
           {"End the scan. Its points (or the scanner's mesh of them) go into the project with scan.export_to_body.",
            "Termina el escaneo. Sus puntos (o la malla que hace el escáner) pasan al proyecto con scan.export_to_body."},
           &ScanBackend::stop, "Scan finished. Next: scan.export_to_body.", "Escaneo terminado. Siguiente: scan.export_to_body.");
    simple("scan.disconnect", {"Disconnect scanner", "Desconectar escáner"},
           {"Close the scanner. Points not yet passed to a body are lost.", "Cierra el escáner. Los puntos que no se hayan pasado a un cuerpo se pierden."},
           &ScanBackend::disconnect, "Scanner disconnected.", "Escáner desconectado.");

    R.add({"scan.export_to_body", "scan", {"Scan to body", "Escaneo a cuerpo"},
           {"Put the scan into the project as a new body (one undo step): its points (as: points, with normals) or, after scan.stop, the scanner's own mesh of them (as: mesh). In millimetres. Then work on it like any imported scan.",
            "Pasa el escaneo al proyecto como un cuerpo nuevo (un paso de deshacer): sus puntos (as: points, con normales) o, tras scan.stop, la malla que hace el escáner (as: mesh). En milímetros. Después se trabaja como cualquier escaneo importado."},
           object({{"as", choice({"Points or the scanner's mesh", "Puntos o la malla del escáner"}, {"points", "mesh"}, "points")},
                   {"name", string({"Name for the body", "Nombre del cuerpo"})}}),
           true, false,
           [](CommandContext& ctx, const json& in) {
               const bool asMesh = in["as"] == "mesh";
               auto m = withScanner(ctx, [&](ScanBackend& b) { return asMesh ? b.mesh() : b.cloud(0); });
               if (!m || m->vertices.empty())
                   throw CommandError("failed", ctx.say("The scan has no points yet.", "El escaneo aún no tiene puntos."));
               const std::string name = in.contains("name") ? in["name"].get<std::string>() : std::string(asMesh ? "Escaneo (malla)" : "Escaneo");
               const Body body = ctx.doc.addBody(name, m, "scanner");
               const std::string n = std::to_string(asMesh ? m->faceCount() : m->vertexCount());
               return json{{"ref", body.ref()},
                           {"name", body.name},
                           {"vertices", m->vertexCount()},
                           {"faces", m->faceCount()},
                           {"summary", asMesh ? ctx.say("Scan added as " + body.ref() + ": " + n + " triangles.", "Escaneo añadido como " + body.ref() + ": " + n + " triángulos.")
                                              : ctx.say("Scan added as " + body.ref() + ": " + n + " points.", "Escaneo añadido como " + body.ref() + ": " + n + " puntos.")}};
           }});
}

}  // namespace orv

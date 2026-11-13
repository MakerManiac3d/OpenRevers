# Escaneo directo: la biblioteca de CrealityScan (fase 6)

OpenRevers puede manejar los escáneres de CrealityScan (Creality Raptor,
Otter, Ferret… fabricados por Orbbec) a través de la biblioteca que instala el
propio CrealityScan, `lib_orbbec_scan.dll`. Es **interoperabilidad**:

- No se distribuye ninguna DLL ni cabecera de Creality u Orbbec.
- Las declaraciones de `src/scan/ObScanApi.h` están escritas por nosotros a
  partir de los nombres, tipos y disposición de estructuras que recogen los
  metadatos IL2CPP de CrealityScan (`global-metadata.dat`, versión 29).
- Todo el análisis se hizo en local con REA, en el equipo del usuario.
- La DLL se carga en tiempo de ejecución con `LoadLibraryExW` desde
  `C:\Program Files\CrealityScan\CrealityScan_Data\Plugins\x86_64`. Otra
  carpeta se elige con `ORV_CREALITYSCAN_DIR` o con
  `scan.use_simulator {on: false, library_dir}`.
- Sin la DLL, OpenRevers compila y funciona igual: `scan.status` dice por qué
  no está disponible. Fuera de Windows no se intenta cargar.

## De dónde salen las firmas

En los metadatos, la clase `OBScan.SDK.OBScanNative` (tipo 12779, imagen
`OBScan.SDK.dll`) declara 220 funciones P/Invoke `obscan_*`, métodos
105171–105390:

- Todas devuelven `OBScanStatus`, un `int` de 32 bits; 0 es éxito.
- Los objetos (contexto, gestión de dispositivos, sesión, escaneo, fotograma,
  imagen…) son punteros opacos (`nint`).
- Las configuraciones van por referencia (`T&`), salvo algunas por valor
  (`OBScanOptimizationConfig`, `OBScanMeshlizationConfig`…).
- Las retrollamadas son `ProgressCallbackDelegate(float)` y
  `StatusCallbackDelegate(OBScanStatus)`.
- Los parámetros están en 112735–113243 (nombres y tipos), los tipos de
  retorno en la colección `return_types` y los campos de las estructuras en
  56637–57219.

Lo que OpenRevers resuelve (`kResolvedFunctions`) es solo el ciclo de
escaneo:

1. Contexto: `obscan_context_create` y `obscan_context_destroy`.
2. Dispositivos: número de dispositivos, índice activo, seleccionar
   dispositivo, información del dispositivo.
3. Sesión: `obscan_context_create_session`, `obscan_session_check_supported`,
   `obscan_session_create_scan` y `obscan_session_destroy`.
4. Escaneo: `preview`, `start`, `pause` y `stop`.
5. Datos: `obscan_scan_get_model_point_color_clouds` y su `release`.
6. Mallado: `obscan_scan_get_default_opt_to_mesh_config`,
   `obscan_scan_optimization_to_mesh` y `obscan_scan_get_optimization_mesh`.

Una prueba (`ScanApi.NeverResolvesNetworkFirmwareOrAiFunctions`) garantiza
que ningún nombre resuelto contiene `AI_`, `upgrade`, `wifi`, `factory`,
`register_state`, `recovery`, `reboot`, `backup_device`, `resume_device`,
`work_mode` ni `3D_GS`.

## Estructuras (64 bits, alineación natural)

| Estructura | Campos | Tamaño |
| --- | --- | --- |
| `OBScanPoint` | `float x, y, z, intensity` | 16 |
| `OBScanNormal` | `float nx, ny, nz, nw` | 16 |
| `OBScanColor` | `uint8 r, g, b, a` | 4 |
| `OBScanPointColorClouds` | `uint32 num_points; Point* points; Normal* normals; Color* colors` | 32 |
| `OBScanTriangleMesh` | `uint32 num_vertices; Point* vertices; Normal* normals; uint32 num_triangles; Triangle* triangles` | 40 |
| `OBScanModeConfig` | 8 enumeraciones, `double voxel_size`, 2 enumeraciones, `double detail_voxel_size` | 56 |
| `OBScanOptimizationConfig` | `compute_speed, float voxel_size, float credible_ratio, BOOL remove_markers, export_format` | 20 |
| `OBScanContextConfig` | 5 rutas | 40 |
| `OBScanSessionConfig` | `output_dir, name, BOOL is_import_mesh_file` | 24 |

`ScanApi.StructureLayoutsMatchTheMetadata` comprueba los tamaños.

## Lo que falta confirmar con un escáner

IL2CPP no guarda los atributos de *marshalling*, así que hay detalles que
solo se confirman con el Raptor conectado:

- **Valores de las enumeraciones:** se toman en el orden de los metadatos
  (0, 1, 2…). `OB_SCAN_SUCCESS = 0` es casi seguro.
- **Cadenas dentro de estructuras:**
  - En las configuraciones de entrada (`ContextConfig`, `SessionConfig`,
    `ScanConfig`) se pasan como `const char*`.
  - En `OBScanDeviceInfo` deben ser tablas de tamaño fijo, de tamaño
    desconocido. OpenRevers lee el `pid` (primer `int`), pasa un bloque de
    4 KB a cero y extrae de él los textos legibles (nombre y número de
    serie).
- **`obscan_version`:** su salida es un puntero; se resuelve pero no se lee
  hasta confirmar que es una cadena.
- **Unidades:** si el modelo mide menos de 5 unidades de diagonal con más de
  1000 puntos, se toma en metros y se pasa a milímetros. Se decide una vez
  por conexión.
- **`input_algo_dependent_files_dir`:** se pasa la carpeta de la DLL. Puede
  que la biblioteca espere otra carpeta de CrealityScan con sus modelos de
  algoritmo.
- **Fotogramas:** no se registran oyentes de mensajes (`FRAME_READY`). Hay
  que confirmar que la biblioteca no acumula fotogramas sin liberar.

## Lo aprendido en la primera prueba con el Raptor (0.10.2)

- La biblioteca (`SDK version 3.5.4`) carga bien desde la carpeta de
  CrealityScan y Orbbec SDK ve el escáner por USB 3.0 (`CR-Scan Raptor`,
  PID `0x06A8`).
- Pero `obscan_device_management_get_device_num` daba 0: **la lista de la
  biblioteca solo se llena si hay un oyente de mensajes de dispositivo**. Sin
  él registra `device callback is nullptr` al conectar el escáner y no lo
  añade.
- De los metadatos de CrealityScan:
  - `obscan_device_management_add_device_msg_listener(device_handle,
    listener)` y su `remove_…`, los dos con punteros.
  - `OBScanDeviceMessageListener { onMessage }`, cuya dirección guarda la
    biblioteca.
  - `onMessage(OBScanDeviceMessage msg)` con `{ int32 msg_id; IntPtr data }`
    (16 bytes por valor).
  - Mensajes en orden: `DEVICE_CONNECT` = 0, `DEVICE_DISCONNECT` = 1, …,
    `DEVICE_CONNECT_NEED_CALIBRATION` = 6.
- OpenRevers registra el oyente al crear el contexto y, si la lista está
  vacía, espera hasta 4 s a que la biblioteca anuncie el escáner.
  `scan.status` da el último mensaje (`deviceMessage`).
- `OBScanDeviceInfo` tiene `camera_pid`, `camera_name`,
  `camera_serial_number`, `camera_connection_type`,
  `camera_firmware_version`, campos Wi-Fi y `device_calibration_timestamp`;
  los textos son de tamaño fijo, aún desconocido.
- El registro avisa `the device config file path is nullptr`. No está en
  `OBScanContextConfig` (5 rutas, confirmado), así que de momento se deja.

## Privacidad

OpenRevers promete que nada sale del equipo. Lo comprobado:

- **`lib_orbbec_scan.dll`** no importa ninguna biblioteca de red (ni
  `WS2_32`, ni `WinHTTP`, ni `WinINet`). Importa `CreateProcess`, COM/WMI,
  registro y `CryptGenRandom`, probablemente para consultar el hardware y la
  GPU.
- **`OrbbecSDK.dll`** es Orbbec SDK v1, de código abierto (Apache-2.0).
  - Usa sockets para descubrir cámaras Ethernet en la red local
    (`ob_enable_net_device_enumeration`) y para un servidor opcional
    (`ob_enable_network_server`). En esa versión, los dos vienen apagados por
    defecto.
  - No es tráfico a internet.
  - **Visto en la primera prueba real (13/11/2026):** la búsqueda de cámaras
    Ethernet sí está activa en la copia de CrealityScan. Su registro
    (`ScannerStreamSDK.log`) muestra un cliente GVCP que se ata a la IP
    local del equipo y a 127.0.0.1 e intenta difundir cada segundo; todos los
    envíos fallan (`sendto` error 10038), así que en la práctica no sale
    nada. Queda por ver si se puede apagar desde OpenRevers.
- La única referencia a internet en los metadatos es
  `OB_SCAN_ERROR_MODEL_GENERATION_FAILED_WITH_NETWORK_EXCEPTION`, de la
  generación por IA. OpenRevers no la resuelve nunca.
- **Pendiente:** buscar URLs dentro de `lib_orbbec_scan.dll` (el análisis de
  cadenas agota el tiempo en remoto) y vigilar la red durante la prueba con
  el Raptor (Monitor de recursos, o una regla de firewall de salida para
  OpenRevers).

## Probar sin escáner

`ORV_SCAN_SIMULATOR=1` (o `scan.use_simulator`) da un escáner simulado: una
placa de 60 × 40 × 8 mm con un agujero de Ø10, con ruido de 0,02 mm, que se
completa en cuatro lecturas mientras «escanea». Sirve para las pruebas, para
el CI y para enseñar el flujo.

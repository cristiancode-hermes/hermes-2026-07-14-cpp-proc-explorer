# proc-explorer — /proc Filesystem Explorer

**proc-explorer** es una herramienta CLI de inspección de procesos Linux que lee el sistema de archivos virtual `/proc` y presenta información detallada de procesos, memoria, CPU y sistema. Desarrollada en **C++23** con las bibliotecas modernas del ecosistema C++.

## Características

- **Listado de procesos** — lista todos los procesos con PID, nombre, estado, RSS, threads y línea de comandos
- **Filtrado** — filtra por nombre (substring) o estado (Running, Sleeping, Zombie, etc.)
- **Ordenación** — ordena por PID o por consumo de memoria
- **Vista detallada** — información completa de un proceso: maps de memoria, descriptores de archivo, variables de entorno
- **Resumen del sistema** — CPU, memoria, uptime, load average de un vistazo
- **Monitor en tiempo real** — modo tipo `top` con actualización periódica
- **Salida JSON** — toda la salida disponible en JSON estructurado para integración con scripts
- **Colores ANSI** — resaltado por estado del proceso y consumo de recursos
- **Cobertura de tests** — tests unitarios, de integración y benchmarks con Catch2

## Stack tecnológico

| Componente | Tecnología |
|-----------|-----------|
| Lenguaje | C++23 (`std::expected`, `std::println`, `std::format`, ranges, string_view) |
| Build | CMake 3.28+ con presets (Unix Makefiles) |
| CLI | CLI11 (header-only via FetchContent) |
| JSON | nlohmann_json (header-only via FetchContent) |
| Logging | spdlog (header-only via FetchContent) |
| Tests | Catch2 v3 (via FetchContent) |
| Docker | multi-stage: gcc:14 → debian:bookworm-slim |

## Requisitos

- **Sistema operativo**: Linux (kernel 5.x+) — lee `/proc`
- **Compilador**: GCC 14+ (C++23 completo)
- **CMake**: 3.28+

## Compilación

### Con CMake presets (recomendado)

```bash
cmake --preset debug
cmake --build --preset debug
./build/debug/proc-explorer --help
```

### Manual

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/proc-explorer --help
```

### Tests

```bash
cmake --preset debug
cmake --build --preset debug
cd build/debug && ctest --output-on-failure -V
```

## Uso

```bash
# Listar todos los procesos
./build/debug/proc-explorer list

# Listar ordenado por memoria (top consumidores)
./build/debug/proc-explorer list --sort-mem --limit 10

# Filtrar por nombre
./build/debug/proc-explorer list --name systemd

# Filtrar por estado (Running)
./build/debug/proc-explorer list --state R

# Resumen del sistema
./build/debug/proc-explorer summary

# Detalle de un proceso específico
./build/debug/proc-explorer detail 1
./build/debug/proc-explorer detail 1 --json

# Monitor en tiempo real (como top)
./build/debug/proc-explorer monitor
./build/debug/proc-explorer monitor --sort-mem --limit 15 --name nginx

# Salida JSON
./build/debug/proc-explorer list --json
./build/debug/proc-explorer summary --json

# Sin colores (para piping)
./build/debug/proc-explorer list --no-color
```

## Estructura del proyecto

```
2026-07-14-cpp-proc-explorer/
├── CMakeLists.txt              # Build C++23, FetchContent
├── CMakePresets.json           # Debug (ASan+UBSan), Release (LTO)
├── Dockerfile                  # Multi-stage gcc:14
├── .gitignore
├── README.md
├── include/
│   └── proc_explorer/
│       └── proc_explorer.hpp   # Header principal con tipos y Result<T>
├── src/
│   ├── proc_explorer.cpp       # Implementación de lectores /proc
│   └── main.cpp                # CLI con subcomandos
├── tests/
│   ├── CMakeLists.txt
│   └── test_main.cpp           # Unit + integration + benchmark
└── docs/
    └── architecture.md         # Diagrama de flujo y decisiones técnicas
```

## Notas técnicas

- `/proc/cpuinfo` usa `key\t: value` (tabulador antes de `:`) — el parser recorta trailing whitespace
- `/proc` no soporta `mmap()` (st_size == 0) — se usa `std::ifstream` para lectura
- `/proc/uptime` segundo campo es tiempo idle **acumulado** de todos los núcleos — puede exceder uptime
- `std::expected` (`<expected>`) maneja errores de I/O sin excepciones — requiere `-std=c++23`
- `std::println` con llaves literales requiere escaparlas como `{{}}`

## Licencia

MIT

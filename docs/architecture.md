# Architecture — proc-explorer

## Flujo de datos

```
/proc filesystem
     │
     ▼
┌─────────────────────────────┐
│    proc_explorer.cpp        │
│  ───────────────────────    │
│  read_proc_file()           │  ← RAII ifstream, maneja st_size==0
│  split_kv()                 │  ← parsea "key\t: value" de /proc
│  read_cpuinfo()             │
│  read_meminfo()             │
│  read_uptime()              │
│  read_loadavg()             │
│  read_proc_stat()           │
│  list_pids()                │  ← opendir/readdir sobre /proc
│  read_process(pid)          │  ← /proc/PID/status + /proc/PID/stat
│  read_maps(pid)             │  ← /proc/PID/maps
│  read_fds(pid)              │  ← /proc/PID/fd (readlink)
│  read_environ(pid)          │  ← /proc/PID/environ
└──────────┬──────────────────┘
           │ Result<T> (std::expected)
           ▼
┌─────────────────────────────┐
│         main.cpp            │
│  ───────────────────────    │
│  CLI11 argument parsing     │
│   ┌────────────────┐        │
│   │ list subcommand │        │  ← lista procesos con filtros
│   │ summary subcmd  │        │  ← resumen del sistema
│   │ detail subcmd   │        │  ← detalle de proceso (maps, fd, env)
│   │ monitor subcmd  │        │  ← refresco periódico tipo top
│   └────────────────┘        │
│  nlohmann_json (--json)     │
│  spdlog (logging)           │
│  ANSI terminal output       │
└─────────────────────────────┘
```

## Decisiones técnicas

### 1. std::expected para error handling
Todos los lectores de /proc retornan `Result<T>` (`std::expected<T, Error>`). Esto evita excepciones en el flujo de I/O y fuerza al llamante a manejar el error explícitamente.

### 2. /proc no soporta mmap
Los archivos virtuales en `/proc` reportan `st_size == 0`, lo que causa `EINVAL` en `mmap()`. Se usa `std::ifstream` con `rdbuf()` para lectura completa.

### 3. split_kv y el tabulador en /proc/cpuinfo
`/proc/cpuinfo` formatea las líneas como `key\t: value` (tabulador antes de `:`). El parser recorta trailing whitespace de la key después del split.

### 4. RAII en recursos del sistema
Los recursos (DIR*, ifstream) se manejan con RAII para garantizar cierre automático incluso en errores.

### 5. CLI11 para parsing de argumentos
CLI11 proporciona validación de argumentos, subcomandos, y autogeneración de `--help`.

### 6. FetchContent para dependencias
Todas las dependencias se obtienen vía CMake FetchContent — no requiere instalación global de paquetes.

## Patrón de extensiones

Para añadir un nuevo lector de /proc:
1. Añadir el struct de datos en `include/proc_explorer/proc_explorer.hpp`
2. Implementar la función en `src/proc_explorer.cpp`
3. Añadir test en `tests/test_main.cpp`

Para añadir un nuevo subcomando CLI:
1. Añadir el subcomando en `src/main.cpp`
2. Implementar el handler
3. Añadir test de integración

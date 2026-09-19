# Generated firmware dependencies

The firmware build fetches these upstream trees into the ignored `_deps/`
directory and pins them by full commit in `tools/bootstrap_deps.py`:

- MicroPython v1.28.0, commit
  `e0e9fbb17ed6fd06bb76e266ae554784c9c80804`, MIT. The official embed
  generator creates a self-contained package under `build/`; HackyLens
  replaces only the stdout port and supplies its own `mpconfigport.h`.
- littlefs v2.11.2, commit
  `adad0fbbcf5382c20978d07f94f9c13be9041c1b`, BSD-3-Clause. The four
  upstream `lfs` source/header files are staged unchanged for firmware builds.

The corresponding license notices are retained under
`firmware/third_party/micropython/` and `firmware/third_party/littlefs/`.

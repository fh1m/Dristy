# HackyLens MicroPython embed-package generator.
#
# MICROPYTHON_TOP and the output directories are supplied by
# tools/build_firmware.py.  This file intentionally delegates source and qstr
# selection to the pinned upstream embed port instead of maintaining a forked
# list of MicroPython internals.

MICROPYTHON_TOP ?= ../../../_deps/micropython
BUILD ?= ../../../build/micropython-embed-work
PACKAGE_DIR ?= ../../../build/micropython_embed
USER_C_MODULES ?= usermod

include $(MICROPYTHON_TOP)/ports/embed/embed.mk

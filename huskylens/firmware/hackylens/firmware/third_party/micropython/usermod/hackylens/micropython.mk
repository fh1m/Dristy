HACKYLENS_USERMOD_DIR := $(USERMOD_DIR)

# The source remains in the feature module so the firmware architecture guard
# and normal SDK compilation own it. The embed generator only scans it for
# qstrs and MP_REGISTER_MODULE declarations.
SRC_USERMOD += $(HACKYLENS_USERMOD_DIR)/../../../../src/apps/micropython/micropython_bindings.c
CFLAGS_USERMOD += -I$(HACKYLENS_USERMOD_DIR)/../../../../include

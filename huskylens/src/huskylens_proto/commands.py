"""HUSKYLENS wire command and algorithm identifiers.

Values come from the official protocol document (v0.5.1) published at
https://github.com/HuskyLens/HUSKYLENSArduino/blob/master/HUSKYLENS%20Protocol.md
"""

from enum import IntEnum


class Command(IntEnum):
    """Every command ID named by the v0.5.1 protocol document."""

    REQUEST = 0x20
    REQUEST_BLOCKS = 0x21
    REQUEST_ARROWS = 0x22
    REQUEST_LEARNED = 0x23
    REQUEST_BLOCKS_LEARNED = 0x24
    REQUEST_ARROWS_LEARNED = 0x25
    REQUEST_BY_ID = 0x26
    REQUEST_BLOCKS_BY_ID = 0x27
    REQUEST_ARROWS_BY_ID = 0x28

    RETURN_INFO = 0x29
    RETURN_BLOCK = 0x2A
    RETURN_ARROW = 0x2B

    REQUEST_KNOCK = 0x2C
    REQUEST_ALGORITHM = 0x2D
    RETURN_OK = 0x2E

    REQUEST_CUSTOMNAMES = 0x2F
    REQUEST_PHOTO = 0x30
    REQUEST_SEND_KNOWLEDGES = 0x32
    REQUEST_RECEIVE_KNOWLEDGES = 0x33
    REQUEST_CUSTOM_TEXT = 0x34
    REQUEST_CLEAR_TEXT = 0x35
    REQUEST_LEARN = 0x36
    REQUEST_FORGET = 0x37
    REQUEST_SAVE_SCREENSHOT = 0x39

    REQUEST_IS_PRO = 0x3B
    RETURN_IS_PRO = 0x3B

    # Listed in the protocol document under a bare heading with no frame
    # layout, no data description, and no example. Behaviour is unknown and is
    # one of the things the command sweep is meant to resolve.
    REQUEST_FIRMWARE_VERSION = 0x3C

    RETURN_BUSY = 0x3D
    RETURN_NEED_PRO = 0x3E


class Algorithm(IntEnum):
    """Algorithm selector values for REQUEST_ALGORITHM."""

    FACE_RECOGNITION = 0x00
    OBJECT_TRACKING = 0x01
    OBJECT_RECOGNITION = 0x02
    LINE_TRACKING = 0x03
    COLOR_RECOGNITION = 0x04
    TAG_RECOGNITION = 0x05
    OBJECT_CLASSIFICATION = 0x06


#: Commands that mutate device state: learned models, SD card contents, the
#: active algorithm, or the on-screen overlay. The command sweep must never
#: send these, and callers get an explicit opt-in for them.
MUTATING_COMMANDS = frozenset(
    {
        Command.REQUEST_ALGORITHM,
        Command.REQUEST_CUSTOMNAMES,
        Command.REQUEST_PHOTO,
        Command.REQUEST_SEND_KNOWLEDGES,
        Command.REQUEST_RECEIVE_KNOWLEDGES,
        Command.REQUEST_CUSTOM_TEXT,
        Command.REQUEST_CLEAR_TEXT,
        Command.REQUEST_LEARN,
        Command.REQUEST_FORGET,
        Command.REQUEST_SAVE_SCREENSHOT,
    }
)

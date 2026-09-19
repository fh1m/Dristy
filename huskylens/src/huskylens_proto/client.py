"""High-level HUSKYLENS client.

Replaces DFRobot's ``huskylib.py``, which is unmaintained, untyped, and silently
swallows protocol errors. Read-only operations are available by default;
anything that changes learned models, SD card contents, the active algorithm or
the screen overlay requires constructing the client with ``allow_mutations``.
"""

from __future__ import annotations

from typing import Callable, Optional

from .commands import MUTATING_COMMANDS, Algorithm, Command
from .frames import Frame, encode_uint16
from .transport import SerialTransport
from .types import Arrow, Block, Info, Results


class ProtocolError(RuntimeError):
    """The device responded, but not in the way the protocol requires."""


class DeviceBusy(ProtocolError):
    """The device returned RETURN_BUSY (0x3D)."""


class ProFeatureRequired(ProtocolError):
    """The device returned RETURN_NEED_PRO (0x3E)."""


class MutationNotAllowed(PermissionError):
    """A state-changing command was attempted on a read-only client."""


class HuskyLens:
    def __init__(
        self,
        transport: SerialTransport,
        allow_mutations: bool = False,
        request_timeout: float = 0.5,
    ) -> None:
        self.transport = transport
        self.allow_mutations = allow_mutations
        self.request_timeout = request_timeout

    # -- lifecycle ---------------------------------------------------------

    @classmethod
    def open(
        cls,
        port: str = "/dev/ttyUSB0",
        baudrate: int = 9600,
        allow_mutations: bool = False,
    ) -> "HuskyLens":
        return cls(
            SerialTransport(port=port, baudrate=baudrate).open(),
            allow_mutations=allow_mutations,
        )

    def close(self) -> None:
        self.transport.close()

    def __enter__(self) -> "HuskyLens":
        return self

    def __exit__(self, *_exc_info: object) -> None:
        self.close()

    # -- plumbing ----------------------------------------------------------

    def _guard(self, command: Command) -> None:
        if command in MUTATING_COMMANDS and not self.allow_mutations:
            raise MutationNotAllowed(
                f"{command.name} changes device state; construct HuskyLens with "
                "allow_mutations=True to permit it"
            )

    def _raise_for_status(self, frame: Frame) -> None:
        if frame.command == Command.RETURN_BUSY:
            raise DeviceBusy("device reported RETURN_BUSY")
        if frame.command == Command.RETURN_NEED_PRO:
            raise ProFeatureRequired("command is HUSKYLENS PRO only")

    def _exchange(
        self,
        command: Command,
        data: bytes = b"",
        timeout: Optional[float] = None,
        stop_when: Optional[Callable[[list[Frame]], bool]] = None,
    ) -> list[Frame]:
        self._guard(command)
        received = self.transport.exchange(
            command,
            data,
            timeout if timeout is not None else self.request_timeout,
            stop_when=stop_when,
        )
        for frame in received:
            self._raise_for_status(frame)
        return received

    @staticmethod
    def _settled(frames: list[Frame]) -> bool:
        """True once a single-frame acknowledgement has arrived."""
        return frames[-1].command in (
            Command.RETURN_OK,
            Command.RETURN_BUSY,
            Command.RETURN_NEED_PRO,
            Command.RETURN_IS_PRO,
        )

    @staticmethod
    def _batch_complete(frames: list[Frame]) -> bool:
        """True once every result promised by RETURN_INFO has been received."""
        promised: Optional[int] = None
        seen = 0
        for frame in frames:
            if frame.command == Command.RETURN_INFO:
                promised = Info.decode(frame.data).result_count
            elif frame.command in (Command.RETURN_BLOCK, Command.RETURN_ARROW):
                seen += 1
        return promised is not None and seen >= promised

    def _expect_ok(self, command: Command, data: bytes = b"") -> bool:
        return any(
            frame.command == Command.RETURN_OK
            for frame in self._exchange(command, data, stop_when=self._settled)
        )

    @staticmethod
    def _collect(received: list[Frame]) -> Results:
        info: Optional[Info] = None
        blocks: list[Block] = []
        arrows: list[Arrow] = []
        for frame in received:
            if frame.command == Command.RETURN_INFO:
                info = Info.decode(frame.data)
            elif frame.command == Command.RETURN_BLOCK:
                blocks.append(Block.decode(frame.data))
            elif frame.command == Command.RETURN_ARROW:
                arrows.append(Arrow.decode(frame.data))
        if info is None:
            raise ProtocolError(
                "no RETURN_INFO in response; got commands "
                + ", ".join(hex(f.command) for f in received)
            )
        return Results(info=info, blocks=tuple(blocks), arrows=tuple(arrows))

    def _query(self, command: Command, data: bytes = b"") -> Results:
        # A batch is RETURN_INFO plus result_count result frames. Stopping as
        # soon as that many have arrived keeps the poll rate bound by the
        # device rather than by our timeout.
        return self._collect(
            self._exchange(
                command,
                data,
                self.request_timeout * 4,
                stop_when=self._batch_complete,
            )
        )

    # -- read-only operations ---------------------------------------------

    def knock(self) -> bool:
        """Return True if the device answers a KNOCK with RETURN_OK."""
        try:
            return self._expect_ok(Command.REQUEST_KNOCK)
        except ProtocolError:
            return False

    def request(self) -> Results:
        """All blocks and arrows."""
        return self._query(Command.REQUEST)

    def request_blocks(self) -> Results:
        return self._query(Command.REQUEST_BLOCKS)

    def request_arrows(self) -> Results:
        return self._query(Command.REQUEST_ARROWS)

    def request_learned(self) -> Results:
        return self._query(Command.REQUEST_LEARNED)

    def request_blocks_learned(self) -> Results:
        return self._query(Command.REQUEST_BLOCKS_LEARNED)

    def request_arrows_learned(self) -> Results:
        return self._query(Command.REQUEST_ARROWS_LEARNED)

    def request_by_id(self, object_id: int) -> Results:
        return self._query(Command.REQUEST_BY_ID, encode_uint16(object_id))

    def request_blocks_by_id(self, object_id: int) -> Results:
        return self._query(Command.REQUEST_BLOCKS_BY_ID, encode_uint16(object_id))

    def request_arrows_by_id(self, object_id: int) -> Results:
        return self._query(Command.REQUEST_ARROWS_BY_ID, encode_uint16(object_id))

    def is_pro(self) -> Optional[bool]:
        """Return True for a PRO unit, False for standard, None if unanswered."""
        for frame in self._exchange(Command.REQUEST_IS_PRO):
            if frame.command == Command.RETURN_IS_PRO and len(frame.data) >= 2:
                return bool(frame.data[0] | (frame.data[1] << 8))
        return None

    def firmware_version(self) -> Optional[bytes]:
        """Send REQUEST_FIRMWARE_VERSION (0x3C) and return any payload.

        The protocol document names this command but never specifies a request
        frame, a response command, or a payload format, so the raw bytes are
        returned for inspection rather than being parsed.
        """
        for frame in self._exchange(Command.REQUEST_FIRMWARE_VERSION):
            if frame.command not in (Command.RETURN_BUSY, Command.RETURN_NEED_PRO):
                return frame.data
        return None

    # -- state-changing operations ----------------------------------------

    def set_algorithm(self, algorithm: Algorithm) -> bool:
        return self._expect_ok(
            Command.REQUEST_ALGORITHM, encode_uint16(int(algorithm))
        )

    def learn(self, object_id: int) -> bool:
        return self._expect_ok(Command.REQUEST_LEARN, encode_uint16(object_id))

    def forget(self) -> bool:
        return self._expect_ok(Command.REQUEST_FORGET)

    def save_photo(self) -> bool:
        return self._expect_ok(Command.REQUEST_PHOTO)

    def save_screenshot(self) -> bool:
        return self._expect_ok(Command.REQUEST_SAVE_SCREENSHOT)

    def save_model(self, file_number: int) -> bool:
        return self._expect_ok(
            Command.REQUEST_SEND_KNOWLEDGES, encode_uint16(file_number)
        )

    def load_model(self, file_number: int) -> bool:
        return self._expect_ok(
            Command.REQUEST_RECEIVE_KNOWLEDGES, encode_uint16(file_number)
        )

    def set_custom_name(self, object_id: int, name: str) -> bool:
        encoded = name.encode("ascii")
        payload = bytes([object_id, len(encoded) + 1]) + encoded + b"\x00"
        return self._expect_ok(Command.REQUEST_CUSTOMNAMES, payload)

    def show_text(self, text: str, x: int, y: int) -> bool:
        encoded = text.encode("ascii")
        if len(encoded) >= 20:
            raise ValueError("custom text must be shorter than 20 characters")
        # X above 255 is signalled by a 0xFF flag byte plus the remainder.
        x_flag, x_value = (0xFF, x % 255) if x >= 255 else (0x00, x)
        payload = bytes([len(encoded), x_flag, x_value, y]) + encoded
        return self._expect_ok(Command.REQUEST_CUSTOM_TEXT, payload)

    def clear_text(self) -> bool:
        return self._expect_ok(Command.REQUEST_CLEAR_TEXT)

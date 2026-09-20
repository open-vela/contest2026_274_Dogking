#!/usr/bin/env python3
#
# Create the small FIT image used by the RK3506 U-Boot AMP loader.
#
# This is a dependency-free fallback for SDK installations where mkimage is
# present but the host dtc executable was not shipped or was lost while the
# SDK was extracted on Windows.
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import struct
from pathlib import Path


FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_END = 9


def align4(data: bytes) -> bytes:
    return data + b"\0" * ((-len(data)) & 3)


class FdtBuilder:
    def __init__(self) -> None:
        self.structure = bytearray()
        self.strings = bytearray()
        self.string_offsets: dict[str, int] = {}

    def tag(self, value: int) -> None:
        self.structure.extend(struct.pack(">I", value))

    def begin_node(self, name: str) -> None:
        self.tag(FDT_BEGIN_NODE)
        self.structure.extend(align4(name.encode("ascii") + b"\0"))

    def end_node(self) -> None:
        self.tag(FDT_END_NODE)

    def string_offset(self, name: str) -> int:
        if name not in self.string_offsets:
            self.string_offsets[name] = len(self.strings)
            self.strings.extend(name.encode("ascii") + b"\0")

        return self.string_offsets[name]

    def prop(self, name: str, value: bytes) -> None:
        self.tag(FDT_PROP)
        self.structure.extend(
            struct.pack(">II", len(value), self.string_offset(name))
        )
        self.structure.extend(align4(value))

    def prop_string(self, name: str, value: str) -> None:
        self.prop(name, value.encode("ascii") + b"\0")

    def prop_u32(self, name: str, value: int) -> None:
        self.prop(name, struct.pack(">I", value))

    def finish(self) -> bytes:
        self.tag(FDT_END)

        header_size = 40
        reserve_map = b"\0" * 16
        structure = bytes(self.structure)
        strings = bytes(self.strings)
        structure_offset = header_size + len(reserve_map)
        strings_offset = structure_offset + len(structure)
        total_size = strings_offset + len(strings)

        header = struct.pack(
            ">10I",
            FDT_MAGIC,
            total_size,
            structure_offset,
            strings_offset,
            header_size,
            17,
            16,
            0,
            len(strings),
            len(structure),
        )

        return header + reserve_map + structure + strings


def build_fit_with_size(firmware: bytes, total_size: int) -> bytes:
    fit = FdtBuilder()

    fit.begin_node("")
    fit.prop_string("description", "RK3506 openvela FIT image")
    fit.prop_u32("#address-cells", 1)
    fit.prop_u32("totalsize", total_size)

    fit.begin_node("images")
    fit.begin_node("amp0")
    fit.prop_string("description", "openvela-cpu0")
    fit.prop("data", firmware)
    fit.prop_string("type", "firmware")
    fit.prop_string("compression", "none")
    fit.prop_string("arch", "arm")
    fit.prop_string("os", "rtos")
    fit.prop_u32("cpu", 0xF00)
    fit.prop_u32("thumb", 0)
    fit.prop_u32("hyp", 0)
    fit.prop_u32("load", 0x00100000)
    fit.prop_u32("entry", 0x00100000)
    fit.prop_u32("udelay", 10000)

    fit.begin_node("hash")
    fit.prop_string("algo", "sha256")
    fit.prop("value", hashlib.sha256(firmware).digest())
    fit.end_node()

    fit.end_node()
    fit.end_node()

    fit.begin_node("configurations")
    fit.prop_string("default", "conf")
    fit.begin_node("conf")
    fit.prop_string("description", "RK3506 openvela on CPU0")
    fit.prop_string("loadables", "amp0")
    fit.end_node()
    fit.end_node()

    fit.end_node()
    return fit.finish()


def build_fit(firmware: bytes) -> bytes:
    # Rockchip's AMP loader reads the root-level "totalsize" property instead
    # of the standard FDT header field.  Adding the property fixes the final
    # size, so a second pass can write its exact value without changing the
    # layout.

    provisional = build_fit_with_size(firmware, 0)
    return build_fit_with_size(firmware, len(provisional))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Pack nuttx.bin as an RK3506 openvela FIT image"
    )
    parser.add_argument("firmware", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    firmware = args.firmware.read_bytes()
    args.output.write_bytes(build_fit(firmware))


if __name__ == "__main__":
    main()

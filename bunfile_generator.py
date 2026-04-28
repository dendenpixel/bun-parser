#!/usr/bin/env python3

"""
BUN file generator and fixture builder.

Provides functions for writing BUN headers and asset records to disk,
and a set of named fixture builders that produce the 22 sample files
used by the test suite.

Usage:
    python3 bunfile_generator.py                    # writes minimal.bun
    python3 bunfile_generator.py --all              # writes all 22 fixtures
    python3 bunfile_generator.py --valid-only       # writes 6 valid fixtures
    python3 bunfile_generator.py --invalid-only     # writes 16 invalid fixtures

BUN format reference: the format spec
"""

import struct
import sys
import argparse
from pathlib import Path

# ============= CONSTANTS =============

HEADER_FIELDS = [
    ("magic", "hex"), ("version_major", "dec"), ("version_minor", "dec"),
    ("asset_count", "dec"), ("asset_table_offset", "dec"),
    ("string_table_offset", "dec"), ("string_table_size", "dec"),
    ("data_section_offset", "dec"), ("data_section_size", "dec"),
    ("reserved", "dec"),
]

RECORD_FIELDS = [
    ("name_offset", "dec"), ("name_length", "dec"),
    ("data_offset", "dec"), ("data_size", "dec"),
    ("uncompressed_size", "dec"), ("compression", "dec"),
    ("asset_type", "dec"), ("checksum", "dec"), ("flags", "dec"),
]

_HEADER_FMT = "<IHHIQQQQQQ"
_RECORD_FMT = "<IIQQQIIII"

BUN_MAGIC         = 0x304E5542
BUN_VERSION_MAJOR = 1
BUN_VERSION_MINOR = 0

COMPRESS_NONE = 0
COMPRESS_RLE  = 1
COMPRESS_ZLIB = 2

FLAG_ENCRYPTED  = 0x1
FLAG_EXECUTABLE = 0x2

HEADER_SIZE = struct.calcsize(_HEADER_FMT)
RECORD_SIZE = struct.calcsize(_RECORD_FMT)
assert HEADER_SIZE == 60
assert RECORD_SIZE == 48

# ============= LOW-LEVEL WRITE HELPERS =============

def write_header(f, *, asset_count, asset_table_offset, string_table_offset,
                 string_table_size, data_section_offset, data_section_size,
                 magic=BUN_MAGIC, version_major=BUN_VERSION_MAJOR,
                 version_minor=BUN_VERSION_MINOR, reserved=0):
    data = struct.pack(_HEADER_FMT, magic, version_major, version_minor,
                       asset_count, asset_table_offset, string_table_offset,
                       string_table_size, data_section_offset,
                       data_section_size, reserved)
    f.write(data)

def write_asset_record(f, *, name_offset, name_length, data_offset, data_size,
                       uncompressed_size=0, compression=COMPRESS_NONE,
                       asset_type=0, checksum=0, flags=0):
    data = struct.pack(_RECORD_FMT, name_offset, name_length, data_offset,
                       data_size, uncompressed_size, compression, asset_type,
                       checksum, flags)
    f.write(data)

def _align4(n):
    return (n + 3) & ~3

def write_padding(f, n):
    if n > 0:
        f.write(b"\x00" * n)


# ============= FIXTURE BUILDERS =============
# Each function builds one specific fixture file. The functions are named
# after the file they produce.

# --- Valid fixtures ---

def build_valid_01_empty(path):
    """Empty BUN: header only, asset_count = 0."""
    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = asset_table_offset
    data_section_offset = asset_table_offset

    with open(path, "wb") as f:
        write_header(f,
            asset_count=0,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=0,
            data_section_offset=data_section_offset,
            data_section_size=0)
        # Pad to common offset (canonical form fills gaps with NULL).
        write_padding(f, asset_table_offset - HEADER_SIZE)

def build_valid_02_alt_empty(path):
    """Empty BUN with a different (still legal) section ordering.
    Spec section 4 says any order is permitted after the header."""
    # Put data section first, then string table, then asset table.
    data_section_offset = _align4(HEADER_SIZE)
    string_table_offset = data_section_offset
    asset_table_offset  = data_section_offset

    with open(path, "wb") as f:
        write_header(f,
            asset_count=0,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=0,
            data_section_offset=data_section_offset,
            data_section_size=0)
        write_padding(f, data_section_offset - HEADER_SIZE)

def build_valid_03_one_asset(path):
    """Single uncompressed asset, with EXECUTABLE flag set (legal)."""
    asset_name    = b"hello"
    asset_payload = b"world"

    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = _align4(len(asset_name))
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(asset_payload))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        write_asset_record(f,
            name_offset=0, name_length=len(asset_name),
            data_offset=0, data_size=len(asset_payload),
            flags=FLAG_EXECUTABLE)
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(asset_name)
        write_padding(f, string_table_size - len(asset_name))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(asset_payload)
        write_padding(f, data_section_size - len(asset_payload))

def build_valid_04_binary_asset(path):
    """Single asset with non-printable (binary) payload."""
    asset_name    = b"icon"
    # Fake PNG header bytes -- intentionally non-printable
    asset_payload = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]) + b"\x00" * 20

    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = _align4(len(asset_name))
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(asset_payload))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        write_asset_record(f,
            name_offset=0, name_length=len(asset_name),
            data_offset=0, data_size=len(asset_payload))
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(asset_name)
        write_padding(f, string_table_size - len(asset_name))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(asset_payload)
        write_padding(f, data_section_size - len(asset_payload))

def build_valid_05_multi_assets_slack(path):
    """7 assets with varied types and gaps between sections."""
    names = [b"alpha", b"beta", b"gamma", b"delta", b"epsilon", b"zeta", b"eta"]
    payloads = [
        b"first content",
        b"second content here",
        b"a third blob",
        b"\x01\x02\x03\x04",       # binary
        b"more text data!!!",
        b"penultimate item",
        b"final asset payload",
    ]
    types = [1, 1, 2, 0, 1, 3, 0]   # texture, audio, etc.

    # Compute offsets with intentional padding "slack" between sections.
    n_assets = len(names)
    asset_table_offset  = _align4(HEADER_SIZE) + 8  # extra slack
    string_table_offset = _align4(asset_table_offset + n_assets * RECORD_SIZE) + 16

    string_block = b""
    name_offsets = []
    name_lengths = []
    for nm in names:
        name_offsets.append(len(string_block))
        name_lengths.append(len(nm))
        string_block += nm
    string_table_size = _align4(len(string_block))

    data_section_offset = _align4(string_table_offset + string_table_size) + 12

    payload_block = b""
    data_offsets = []
    data_sizes   = []
    for p in payloads:
        data_offsets.append(len(payload_block))
        data_sizes.append(len(p))
        payload_block += p
    data_section_size = _align4(len(payload_block))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=n_assets,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        for i in range(n_assets):
            write_asset_record(f,
                name_offset=name_offsets[i], name_length=name_lengths[i],
                data_offset=data_offsets[i], data_size=data_sizes[i],
                asset_type=types[i])
        write_padding(f, string_table_offset - (asset_table_offset + n_assets * RECORD_SIZE))
        f.write(string_block)
        write_padding(f, string_table_size - len(string_block))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(payload_block)
        write_padding(f, data_section_size - len(payload_block))

def build_valid_06_rle(path):
    """RLE-compressed asset: (10, 'A')(5, 'B') = 'AAAAAAAAAABBBBB'."""
    asset_name = b"rle_asset"
    rle_data   = bytes([10, ord('A'), 5, ord('B')])
    expanded_size = 15

    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = _align4(len(asset_name))
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(rle_data))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        write_asset_record(f,
            name_offset=0, name_length=len(asset_name),
            data_offset=0, data_size=len(rle_data),
            uncompressed_size=expanded_size,
            compression=COMPRESS_RLE)
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(asset_name)
        write_padding(f, string_table_size - len(asset_name))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(rle_data)
        write_padding(f, data_section_size - len(rle_data))

# --- Invalid fixtures ---
# Each starts from a known-good layout and corrupts exactly one thing.

def build_invalid_01_bad_magic(path):
    """Wrong magic bytes (spec 9.1)."""
    build_valid_01_empty(path)
    # Overwrite the first 4 bytes (magic) with junk.
    with open(path, "r+b") as f:
        f.seek(0)
        f.write(b"\xDE\xAD\xBE\xEF")

def build_invalid_02_bad_version(path):
    """Unsupported version (spec 4.1.7)."""
    build_valid_01_empty(path)
    with open(path, "r+b") as f:
        f.seek(4)   # version_major is at offset 4
        f.write(struct.pack("<H", 99))

def build_invalid_03_bad_offset_alignment(path):
    """asset_table_offset not divisible by 4 (spec 4.1.3)."""
    asset_table_offset = 65   # not a multiple of 4
    string_table_offset = 100
    data_section_offset = 100

    with open(path, "wb") as f:
        write_header(f,
            asset_count=0,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=0,
            data_section_offset=data_section_offset,
            data_section_size=0)
        write_padding(f, 100 - HEADER_SIZE)   # pad up to 100 bytes total

def build_invalid_04_section_past_eof(path):
    """data_section claims to extend past end of file (spec 9.2)."""
    # File is only 60 bytes (just the header), but we claim a 1000-byte data section.
    with open(path, "wb") as f:
        write_header(f,
            asset_count=0,
            asset_table_offset=60,
            string_table_offset=60,
            string_table_size=0,
            data_section_offset=60,
            data_section_size=1000)   # liar

def build_invalid_05_overlapping_sections(path):
    """string_table and data_section overlap (spec 9.3)."""
    # string_table: offset 60, size 80 -> covers [60, 140)
    # data_section: offset 100, size 80 -> covers [100, 180)
    # Overlap: [100, 140)
    with open(path, "wb") as f:
        write_header(f,
            asset_count=0,
            asset_table_offset=60,
            string_table_offset=60,
            string_table_size=80,
            data_section_offset=100,
            data_section_size=80)
        write_padding(f, 200 - HEADER_SIZE)

def build_invalid_06_name_past_string_table(path):
    """name_offset + name_length > string_table_size (spec 9.5)."""
    asset_name = b"hello"
    asset_payload = b"world"
    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = 8
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(asset_payload))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        # Claim name_length=20 but the table is only 8 bytes
        write_asset_record(f,
            name_offset=0, name_length=20,
            data_offset=0, data_size=len(asset_payload))
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(asset_name)
        write_padding(f, string_table_size - len(asset_name))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(asset_payload)
        write_padding(f, data_section_size - len(asset_payload))

def build_invalid_07_name_nonprintable(path):
    """Asset name contains a 0x01 byte (spec 5)."""
    bad_name = b"he\x01lo"   # 5 bytes but not all printable ASCII
    payload  = b"world"
    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = _align4(len(bad_name))
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(payload))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        write_asset_record(f,
            name_offset=0, name_length=len(bad_name),
            data_offset=0, data_size=len(payload))
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(bad_name)
        write_padding(f, string_table_size - len(bad_name))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(payload)
        write_padding(f, data_section_size - len(payload))

def build_invalid_08_truncated_file(path):
    """File is shorter than even the 60-byte header."""
    with open(path, "wb") as f:
        f.write(b"truncated\n")   # 10 bytes, way less than a header

def build_invalid_09_misaligned_section_size(path):
    """string_table_size not divisible by 4 (spec 4.1.3)."""
    with open(path, "wb") as f:
        write_header(f,
            asset_count=0,
            asset_table_offset=60,
            string_table_offset=60,
            string_table_size=5,   # not a multiple of 4
            data_section_offset=68,
            data_section_size=0)
        write_padding(f, 68 - HEADER_SIZE)

def build_invalid_10_overlap_and_nonprintable(path):
    """TWO defects: overlap AND non-printable name. Tests multi-violation."""
    bad_name = b"a\x02b"   # 3 bytes, non-printable
    payload  = b"data"

    # Make sections overlap on purpose.
    asset_table_offset  = 60
    string_table_offset = 108
    string_table_size   = 8
    data_section_offset = 112   # OVERLAPS string table at [112, 116)
    data_section_size   = 4

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        write_asset_record(f,
            name_offset=0, name_length=len(bad_name),
            data_offset=0, data_size=len(payload))
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(bad_name)
        write_padding(f, string_table_size - len(bad_name))
        # Note: data section overlaps part of the string table padding above.
        # Total file size is data_section_offset + data_section_size = 116.
        # Already past that; truncate.
    # Truncate to exactly cover the overlapping region.
    with open(path, "r+b") as f:
        f.truncate(116)
        f.seek(112)
        f.write(payload)

def build_invalid_11_second_asset_empty_name(path):
    """Two assets; the second has name_length = 0 (spec 5)."""
    name1 = b"first"
    payload = b"data"
    n_assets = 2
    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + n_assets * RECORD_SIZE)
    string_table_size   = _align4(len(name1))
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(payload))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=n_assets,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        # Asset 0 -- valid.
        write_asset_record(f,
            name_offset=0, name_length=len(name1),
            data_offset=0, data_size=len(payload))
        # Asset 1 -- name_length is zero (illegal per spec 5).
        write_asset_record(f,
            name_offset=0, name_length=0,
            data_offset=0, data_size=0)
        write_padding(f, string_table_offset - (asset_table_offset + n_assets * RECORD_SIZE))
        f.write(name1)
        write_padding(f, string_table_size - len(name1))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(payload)
        write_padding(f, data_section_size - len(payload))

def build_invalid_12_name_oob(path):
    """name_offset itself sits past the end of the string table (spec 9.5)."""
    name = b"hi"
    payload = b"data"
    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = 8
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(payload))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        # name_offset = 20, which is past the 8-byte string table.
        write_asset_record(f,
            name_offset=20, name_length=2,
            data_offset=0, data_size=len(payload))
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(name)
        write_padding(f, string_table_size - len(name))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(payload)
        write_padding(f, data_section_size - len(payload))

def build_invalid_13_empty_name(path):
    """Single asset with name_length = 0."""
    payload = b"data"
    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = 4
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(payload))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        write_asset_record(f,
            name_offset=0, name_length=0,
            data_offset=0, data_size=len(payload))
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        write_padding(f, string_table_size)   # empty table
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(payload)
        write_padding(f, data_section_size - len(payload))

def build_invalid_14_rle_zero_count(path):
    """RLE pair (0, X): count of zero is illegal per spec 8.2 note 2."""
    name = b"test"
    rle_data = bytes([10, ord('A'), 0, ord('B')])   # second pair has count=0
    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = _align4(len(name))
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(rle_data))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        write_asset_record(f,
            name_offset=0, name_length=len(name),
            data_offset=0, data_size=len(rle_data),
            uncompressed_size=10,   # if the zero pair were valid, total = 10
            compression=COMPRESS_RLE)
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(name)
        write_padding(f, string_table_size - len(name))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(rle_data)
        write_padding(f, data_section_size - len(rle_data))

def build_invalid_15_rle_bomb(path):
    """RLE expands to 200 bytes but uncompressed_size says 100. Bomb attack."""
    name = b"bomb"
    rle_data = bytes([200, 0xFF])   # one pair: 200 copies of 0xFF
    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = _align4(len(name))
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(rle_data))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        write_asset_record(f,
            name_offset=0, name_length=len(name),
            data_offset=0, data_size=len(rle_data),
            uncompressed_size=100,   # claims 100, actual expansion = 200
            compression=COMPRESS_RLE)
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(name)
        write_padding(f, string_table_size - len(name))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(rle_data)
        write_padding(f, data_section_size - len(rle_data))

def build_invalid_16_rle_truncated(path):
    """RLE expands to 10 bytes but uncompressed_size says 100. Truncated."""
    name = b"test"
    rle_data = bytes([10, ord('A')])   # one pair: 10 copies of 'A'
    asset_table_offset  = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + RECORD_SIZE)
    string_table_size   = _align4(len(name))
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size   = _align4(len(rle_data))

    with open(path, "wb") as f:
        write_header(f,
            asset_count=1,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size)
        write_padding(f, asset_table_offset - HEADER_SIZE)
        write_asset_record(f,
            name_offset=0, name_length=len(name),
            data_offset=0, data_size=len(rle_data),
            uncompressed_size=100,   # claims 100, actual expansion = 10
            compression=COMPRESS_RLE)
        write_padding(f, string_table_offset - (asset_table_offset + RECORD_SIZE))
        f.write(name)
        write_padding(f, string_table_size - len(name))
        write_padding(f, data_section_offset - (string_table_offset + string_table_size))
        f.write(rle_data)
        write_padding(f, data_section_size - len(rle_data))


# ============= REGISTRY =============

VALID_FIXTURES = [
    ("01-empty.bun",                  build_valid_01_empty),
    ("02-alt-empty.bun",              build_valid_02_alt_empty),
    ("03-one-asset.bun",              build_valid_03_one_asset),
    ("04-binary-asset.bun",           build_valid_04_binary_asset),
    ("05-multi-assets-slack.bun",     build_valid_05_multi_assets_slack),
    ("06-rle-valid.bun",              build_valid_06_rle),
]

INVALID_FIXTURES = [
    ("01-bad-magic.bun",                       build_invalid_01_bad_magic),
    ("02-bad-version.bun",                     build_invalid_02_bad_version),
    ("03-bad-offset-alignment.bun",            build_invalid_03_bad_offset_alignment),
    ("04-section-past-eof.bun",                build_invalid_04_section_past_eof),
    ("05-overlapping-sections.bun",            build_invalid_05_overlapping_sections),
    ("06-asset-name-past-string-table.bun",    build_invalid_06_name_past_string_table),
    ("07-asset-name-nonprintable.bun",         build_invalid_07_name_nonprintable),
    ("08-truncated-file.bun",                  build_invalid_08_truncated_file),
    ("09-misaligned-section-size.bun",         build_invalid_09_misaligned_section_size),
    ("10-overlapping-with-nonprintable.bun",   build_invalid_10_overlap_and_nonprintable),
    ("11-second-asset-empty-name.bun",         build_invalid_11_second_asset_empty_name),
    ("12-asset-name-oob.bun",                  build_invalid_12_name_oob),
    ("13-asset-empty-name.bun",                build_invalid_13_empty_name),
    ("14-rle-zero-count.bun",                  build_invalid_14_rle_zero_count),
    ("15-rle-bomb.bun",                        build_invalid_15_rle_bomb),
    ("16-rle-truncated.bun",                   build_invalid_16_rle_truncated),
]


# ============= COMMAND-LINE INTERFACE =============

def generate_all(out_dir, valid=True, invalid=True):
    """Generate all the registered fixture files into out_dir/{valid,invalid}/."""
    out_dir = Path(out_dir)
    if valid:
        valid_dir = out_dir / "valid"
        valid_dir.mkdir(parents=True, exist_ok=True)
        for name, builder in VALID_FIXTURES:
            path = valid_dir / name
            builder(path)
            print(f"  wrote {path} ({path.stat().st_size} bytes)")
    if invalid:
        invalid_dir = out_dir / "invalid"
        invalid_dir.mkdir(parents=True, exist_ok=True)
        for name, builder in INVALID_FIXTURES:
            path = invalid_dir / name
            builder(path)
            print(f"  wrote {path} ({path.stat().st_size} bytes)")

def main():
    parser = argparse.ArgumentParser(description="Generate BUN fixture files.")
    parser.add_argument("--all", action="store_true",
                        help="Generate all 22 fixtures (valid + invalid)")
    parser.add_argument("--valid-only", action="store_true",
                        help="Generate only the 6 valid fixtures")
    parser.add_argument("--invalid-only", action="store_true",
                        help="Generate only the 16 invalid fixtures")
    parser.add_argument("--out-dir", default="tests/fixtures",
                        help="Output directory (default: tests/fixtures)")
    args = parser.parse_args()

    if args.all:
        generate_all(args.out_dir, valid=True, invalid=True)
    elif args.valid_only:
        generate_all(args.out_dir, valid=True, invalid=False)
    elif args.invalid_only:
        generate_all(args.out_dir, valid=False, invalid=True)
    else:
        # Default: just write the original minimal.bun for backwards compatibility.
        build_valid_03_one_asset(Path("minimal.bun"))
        print("Wrote minimal.bun. Use --all to generate full test fixtures.")

if __name__ == "__main__":
    sys.exit(main())
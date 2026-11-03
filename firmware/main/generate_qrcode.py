#!/usr/bin/env python3
"""Build-time QR code generator for Mini E-Reader firmware.

Generates a C header file defining a 1-bit packed monochrome bitmap array
for the specified URL. Uses pure Python standard library with optional
qrcode package fallback.
"""

import argparse
import os
import sys

EXP = [0] * 512
LOG = [0] * 256
_x = 1
for _i in range(255):
    EXP[_i] = _x
    EXP[_i + 255] = _x
    LOG[_x] = _i
    _x <<= 1
    if _x & 0x100:
        _x ^= 0x11D


def gf_mul(a, b):
    if a == 0 or b == 0:
        return 0
    return EXP[LOG[a] + LOG[b]]


def rs_gen(n):
    g = [1]
    for i in range(n):
        root = EXP[i]
        new_g = [0] * (len(g) + 1)
        for j in range(len(g)):
            new_g[j] ^= g[j]
            new_g[j + 1] ^= gf_mul(g[j], root)
        g = new_g
    return g


def rs_encode(data, n_ec):
    g = rs_gen(n_ec)
    res = list(data) + [0] * n_ec
    for i in range(len(data)):
        coef = res[i]
        if coef != 0:
            for j in range(len(g)):
                res[i + j] ^= gf_mul(g[j], coef)
    return res[len(data):]


QR_PARAMS = {
    (2, "M"): (25, 16, 28, 18),
    (2, "L"): (25, 10, 34, 18),
    (3, "M"): (29, 26, 44, 22),
    (3, "L"): (29, 15, 55, 22),
}


def _bch_digit(data):
    digit = 0
    while data != 0:
        digit += 1
        data >>= 1
    return digit


def _bch_type_info(data):
    g15 = 0x537
    g15_mask = 0x5412
    d = data << 10
    while _bch_digit(d) - _bch_digit(g15) >= 0:
        d ^= g15 << (_bch_digit(d) - _bch_digit(g15))
    return ((data << 10) | d) ^ g15_mask


def _mask_func(mask_id, r, c):
    if mask_id == 0:
        return (r + c) % 2 == 0
    if mask_id == 1:
        return r % 2 == 0
    if mask_id == 2:
        return c % 3 == 0
    if mask_id == 3:
        return (r + c) % 3 == 0
    if mask_id == 4:
        return (r // 2 + c // 3) % 2 == 0
    if mask_id == 5:
        return ((r * c) % 2) + ((r * c) % 3) == 0
    if mask_id == 6:
        return (((r * c) % 2) + ((r * c) % 3)) % 2 == 0
    if mask_id == 7:
        return (((r + c) % 2) + ((r * c) % 3)) % 2 == 0
    return False


def _calc_penalty(modules):
    size = len(modules)
    lost = 0

    # Level 1: runs >= 5
    for r in range(size):
        row = modules[r]
        color = row[0]
        length = 0
        for c in range(size):
            if row[c] == color:
                length += 1
            else:
                if length >= 5:
                    lost += 3 + (length - 5)
                length = 1
                color = row[c]
        if length >= 5:
            lost += 3 + (length - 5)

    for c in range(size):
        color = modules[0][c]
        length = 0
        for r in range(size):
            if modules[r][c] == color:
                length += 1
            else:
                if length >= 5:
                    lost += 3 + (length - 5)
                length = 1
                color = modules[r][c]
        if length >= 5:
            lost += 3 + (length - 5)

    # Level 2: 2x2 blocks
    for r in range(size - 1):
        for c in range(size - 1):
            if modules[r][c] == modules[r + 1][c] == modules[r][c + 1] == modules[r + 1][c + 1]:
                lost += 3

    # Level 3: 1011101 patterns
    p1 = [True, False, True, True, True, False, True, False, False, False, False]
    p2 = [False, False, False, False, True, False, True, True, True, False, True]
    for r in range(size):
        row = modules[r]
        for c in range(size - 10):
            sub = row[c:c + 11]
            if sub == p1 or sub == p2:
                lost += 40
    for c in range(size):
        col = [modules[r][c] for r in range(size)]
        for r in range(size - 10):
            sub = col[r:r + 11]
            if sub == p1 or sub == p2:
                lost += 40

    # Level 4: dark ratio
    dark = sum(1 for r in range(size) for c in range(size) if modules[r][c])
    percent = (dark * 100) / (size * size)
    lost += int(abs(percent - 50) / 5) * 10

    return lost


def _setup_type_info(m, size, ec, mask_pattern):
    ec_val = {"L": 1, "M": 0, "Q": 3, "H": 2}[ec]
    data = (ec_val << 3) | mask_pattern
    bits = _bch_type_info(data)

    for i in range(15):
        mod = ((bits >> i) & 1) == 1
        if i < 6:
            m[i][8] = 1 if mod else 0
        elif i < 8:
            m[i + 1][8] = 1 if mod else 0
        else:
            m[size - 15 + i][8] = 1 if mod else 0

    for i in range(15):
        mod = ((bits >> i) & 1) == 1
        if i < 8:
            m[8][size - i - 1] = 1 if mod else 0
        elif i < 9:
            m[8][15 - i - 1 + 1] = 1 if mod else 0
        else:
            m[8][15 - i - 1] = 1 if mod else 0

    m[size - 8][8] = 1


def generate_qr_matrix_builtin(text, version=2, ec="M"):
    key = (version, ec)
    if key not in QR_PARAMS:
        raise ValueError(f"Unsupported QR version/ec pair: {key}")

    size, ec_count, data_count, align_coord = QR_PARAMS[key]
    raw = text.encode("utf-8")
    bits = [0, 1, 0, 0]  # Byte mode
    cnt = len(raw)
    for i in range(7, -1, -1):
        bits.append((cnt >> i) & 1)
    for b in raw:
        for i in range(7, -1, -1):
            bits.append((b >> i) & 1)

    rem = data_count * 8 - len(bits)
    if rem < 0:
        raise ValueError(f"Data length {len(raw)} exceeds QR Version {version}-{ec} capacity")

    for _ in range(min(4, rem)):
        bits.append(0)
    while len(bits) % 8 != 0:
        bits.append(0)

    pad = [0xEC, 0x11]
    p_idx = 0
    while len(bits) < data_count * 8:
        b = pad[p_idx]
        p_idx = (p_idx + 1) % 2
        for i in range(7, -1, -1):
            bits.append((b >> i) & 1)

    data_bytes = []
    for i in range(0, len(bits), 8):
        byte = 0
        for bit in bits[i:i + 8]:
            byte = (byte << 1) | bit
        data_bytes.append(byte)

    ec_bytes = rs_encode(data_bytes, ec_count)
    all_codewords = data_bytes + ec_bytes

    all_bits = []
    for b in all_codewords:
        for i in range(7, -1, -1):
            all_bits.append((b >> i) & 1)

    matrix = [[None] * size for _ in range(size)]
    is_func = [[False] * size for _ in range(size)]

    def set_func(r, c, val):
        matrix[r][c] = val
        is_func[r][c] = True

    def add_finder(r0, c0):
        for r in range(7):
            for c in range(7):
                val = 1 if (r in (0, 6) or c in (0, 6) or (2 <= r <= 4 and 2 <= c <= 4)) else 0
                set_func(r0 + r, c0 + c, val)

    add_finder(0, 0)
    add_finder(0, size - 7)
    add_finder(size - 7, 0)

    # Separators
    for i in range(8):
        for r, c in [(7, i), (i, 7), (7, size - 1 - i), (i, size - 8), (size - 8, i), (size - 1 - i, 7)]:
            if 0 <= r < size and 0 <= c < size:
                set_func(r, c, 0)

    # Alignment pattern
    ar, ac = align_coord, align_coord
    for dr in range(-2, 3):
        for dc in range(-2, 3):
            val = 1 if (abs(dr) == 2 or abs(dc) == 2 or (dr == 0 and dc == 0)) else 0
            set_func(ar + dr, ac + dc, val)

    # Timing patterns
    for i in range(8, size - 8):
        val = 1 if i % 2 == 0 else 0
        set_func(6, i, val)
        set_func(i, 6, val)

    # Dark module
    set_func(size - 8, 8, 1)

    # Reserve format info
    for i in range(9):
        if not is_func[8][i]:
            set_func(8, i, 0)
        if not is_func[i][8]:
            set_func(i, 8, 0)
    for i in range(8):
        if not is_func[8][size - 1 - i]:
            set_func(8, size - 1 - i, 0)
        if not is_func[size - 1 - i][8]:
            set_func(size - 1 - i, 8, 0)

    # Place data bits
    bit_idx = 0
    right = size - 1
    up = True
    while right > 0:
        if right == 6:
            right -= 1
        rows = range(size - 1, -1, -1) if up else range(size)
        for r in rows:
            for c in (right, right - 1):
                if not is_func[r][c]:
                    b = all_bits[bit_idx] if bit_idx < len(all_bits) else 0
                    matrix[r][c] = b
                    bit_idx += 1
        up = not up
        right -= 2

    best_mask = 0
    min_pen = 10**9
    for mask_id in range(8):
        m = [row[:] for row in matrix]
        for r in range(size):
            for c in range(size):
                if not is_func[r][c]:
                    if _mask_func(mask_id, r, c):
                        m[r][c] ^= 1
        bool_m = [[bool(cell) for cell in row] for row in m]
        pen = _calc_penalty(bool_m)
        if pen < min_pen:
            min_pen = pen
            best_mask = mask_id

    final_m = [row[:] for row in matrix]
    for r in range(size):
        for c in range(size):
            if not is_func[r][c]:
                if _mask_func(best_mask, r, c):
                    final_m[r][c] ^= 1
    _setup_type_info(final_m, size, ec, best_mask)
    return [[bool(cell) for cell in row] for row in final_m]


def generate_qr_matrix(text, version=2, ec="M"):
    try:
        import qrcode

        ec_map = {
            "L": qrcode.constants.ERROR_CORRECT_L,
            "M": qrcode.constants.ERROR_CORRECT_M,
            "Q": qrcode.constants.ERROR_CORRECT_Q,
            "H": qrcode.constants.ERROR_CORRECT_H,
        }
        qr = qrcode.QRCode(
            version=version,
            error_correction=ec_map.get(ec, qrcode.constants.ERROR_CORRECT_M),
            box_size=1,
            border=0,
        )
        qr.add_data(text)
        qr.make(fit=False)
        return qr.get_matrix()
    except ImportError:
        return generate_qr_matrix_builtin(text, version, ec)


def pack_qr_bitmap(matrix):
    size = len(matrix)
    total_bits = size * size
    total_bytes = (total_bits + 7) // 8
    bitmap = bytearray(total_bytes)

    for y in range(size):
        for x in range(size):
            if matrix[y][x]:
                bit_idx = y * size + x
                bitmap[bit_idx // 8] |= (1 << (7 - (bit_idx % 8)))

    return bitmap


def generate_header(url, output_path, version=2, ec="M"):
    matrix = generate_qr_matrix(url, version=version, ec=ec)
    size = len(matrix)
    bitmap = pack_qr_bitmap(matrix)
    num_bytes = len(bitmap)

    lines = [
        "// Generated by generate_qrcode.py. Do not edit directly.",
        f"// Source URL: {url}",
        f"// Dimensions: {size}x{size} (Version {version}-{ec}), 1-bit packed bitmap",
        "",
        "#ifndef SYNC_QRCODE_H",
        "#define SYNC_QRCODE_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define SYNC_QRCODE_SIZE {size}",
        "#define SYNC_QRCODE_BYTES ((SYNC_QRCODE_SIZE * SYNC_QRCODE_SIZE + 7) / 8)",
        "",
        f"static const uint8_t s_sync_qrcode_bitmap[SYNC_QRCODE_BYTES] = {{",
    ]

    bytes_per_row = 12
    for i in range(0, num_bytes, bytes_per_row):
        chunk = bitmap[i:i + bytes_per_row]
        hex_items = [f"0x{b:02x}" for b in chunk]
        line = "    " + ", ".join(hex_items) + ","
        lines.append(line)

    lines.extend([
        "};",
        "",
        "#endif // SYNC_QRCODE_H",
        "",
    ])

    header_content = "\n".join(lines)
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(header_content)

    print(f"Generated {output_path} ({size}x{size}, {num_bytes} bytes) for {url}")


def main():
    parser = argparse.ArgumentParser(description="Generate C header with packed QR code bitmap.")
    parser.add_argument(
        "--url",
        default="https://bacain.khadr.dev",
        help="Target URL for QR code (default: https://bacain.khadr.dev)",
    )
    parser.add_argument(
        "--output",
        default="sync_qrcode.h",
        help="Output header file path (default: sync_qrcode.h)",
    )
    parser.add_argument(
        "--version",
        type=int,
        default=2,
        choices=[2, 3],
        help="QR version (default: 2)",
    )
    parser.add_argument(
        "--ec",
        default="M",
        choices=["L", "M"],
        help="Error correction level (default: M)",
    )

    args = parser.parse_args()
    generate_header(args.url, args.output, version=args.version, ec=args.ec)


if __name__ == "__main__":
    main()

from pathlib import Path


GF_EXP = [0] * 512
GF_LOG = [0] * 256


def init_gf():
    x = 1
    for i in range(255):
        GF_EXP[i] = x
        GF_LOG[x] = i
        x <<= 1
        if x & 0x100:
            x ^= 0x11D
    for i in range(255, 512):
        GF_EXP[i] = GF_EXP[i - 255]


def gf_mul(a, b):
    if a == 0 or b == 0:
        return 0
    return GF_EXP[GF_LOG[a] + GF_LOG[b]]


def reed_solomon_generator(degree):
    poly = [1]
    for i in range(degree):
        next_poly = [0] * (len(poly) + 1)
        for j, coefficient in enumerate(poly):
            next_poly[j] ^= gf_mul(coefficient, GF_EXP[i])
            next_poly[j + 1] ^= coefficient
        poly = next_poly
    return poly


def reed_solomon_remainder(data, degree):
    generator = reed_solomon_generator(degree)
    result = [0] * degree
    for value in data:
        factor = value ^ result[0]
        result = result[1:] + [0]
        for i in range(degree):
            result[i] ^= gf_mul(generator[i], factor)
    return result


def bits_from_int(value, count):
    return [(value >> i) & 1 for i in range(count - 1, -1, -1)]


def make_data_codewords(text):
    payload = text.encode("iso-8859-1")
    bits = []
    bits += [0, 1, 0, 0]  # byte mode
    bits += bits_from_int(len(payload), 8)
    for byte in payload:
        bits += bits_from_int(byte, 8)

    # Version 1-L has 19 data codewords, enough for these fixed test labels.
    capacity = 19 * 8
    bits += [0] * min(4, capacity - len(bits))
    while len(bits) % 8:
        bits.append(0)

    codewords = []
    for i in range(0, len(bits), 8):
        value = 0
        for bit in bits[i:i + 8]:
            value = (value << 1) | bit
        codewords.append(value)

    pads = [0xEC, 0x11]
    pad_index = 0
    while len(codewords) < 19:
        codewords.append(pads[pad_index % 2])
        pad_index += 1
    return codewords


def draw_finder(modules, reserved, top, left):
    pattern = [
        "1111111",
        "1000001",
        "1011101",
        "1011101",
        "1011101",
        "1000001",
        "1111111",
    ]
    for y in range(-1, 8):
        for x in range(-1, 8):
            row = top + y
            col = left + x
            if not (0 <= row < 21 and 0 <= col < 21):
                continue
            reserved[row][col] = True
            if 0 <= y < 7 and 0 <= x < 7:
                modules[row][col] = pattern[y][x] == "1"
            else:
                modules[row][col] = False


def reserve_format_areas(reserved):
    coords = set()
    for i in range(9):
        coords.add((8, i))
        coords.add((i, 8))
    for i in range(8):
        coords.add((8, 20 - i))
        coords.add((20 - i, 8))
    for row, col in coords:
        if 0 <= row < 21 and 0 <= col < 21:
            reserved[row][col] = True


def mask_bit(mask, row, col):
    if mask == 0:
        return (row + col) % 2 == 0
    if mask == 1:
        return row % 2 == 0
    if mask == 2:
        return col % 3 == 0
    if mask == 3:
        return (row + col) % 3 == 0
    if mask == 4:
        return (row // 2 + col // 3) % 2 == 0
    if mask == 5:
        return ((row * col) % 2 + (row * col) % 3) == 0
    if mask == 6:
        return (((row * col) % 2 + (row * col) % 3) % 2) == 0
    return (((row + col) % 2 + (row * col) % 3) % 2) == 0


def bch_format_bits(format_data):
    value = format_data << 10
    generator = 0x537
    for i in range(14, 9, -1):
        if (value >> i) & 1:
            value ^= generator << (i - 10)
    return ((format_data << 10) | value) ^ 0x5412


def add_format_bits(modules, mask):
    # Error correction level L is 01 in QR format data.
    bits = bch_format_bits((1 << 3) | mask)
    coords_a = [
        (8, 0), (8, 1), (8, 2), (8, 3), (8, 4), (8, 5),
        (8, 7), (8, 8), (7, 8), (5, 8), (4, 8), (3, 8),
        (2, 8), (1, 8), (0, 8),
    ]
    coords_b = [
        (20, 8), (19, 8), (18, 8), (17, 8), (16, 8), (15, 8),
        (14, 8), (13, 8), (8, 20), (8, 19), (8, 18), (8, 17),
        (8, 16), (8, 15), (8, 14),
    ]
    for i in range(15):
        bit = ((bits >> i) & 1) == 1
        row, col = coords_a[i]
        modules[row][col] = bit
        row, col = coords_b[i]
        modules[row][col] = bit
    modules[13][8] = True


def penalty_score(modules):
    score = 0
    size = 21
    for rows in (modules, list(map(list, zip(*modules)))):
        for row in rows:
            run_color = row[0]
            run_length = 1
            for value in row[1:]:
                if value == run_color:
                    run_length += 1
                else:
                    if run_length >= 5:
                        score += 3 + run_length - 5
                    run_color = value
                    run_length = 1
            if run_length >= 5:
                score += 3 + run_length - 5

    for row in range(size - 1):
        for col in range(size - 1):
            color = modules[row][col]
            if (
                modules[row + 1][col] == color
                and modules[row][col + 1] == color
                and modules[row + 1][col + 1] == color
            ):
                score += 3

    finder_like = [True, False, True, True, True, False, True, False, False, False, False]
    for rows in (modules, list(map(list, zip(*modules)))):
        for row in rows:
            for i in range(size - 10):
                segment = row[i:i + 11]
                if segment == finder_like or segment == finder_like[::-1]:
                    score += 40

    dark = sum(1 for row in modules for value in row if value)
    percent = dark * 100 // (size * size)
    score += abs(percent - 50) // 5 * 10
    return score


def make_qr_matrix(text):
    modules = [[False] * 21 for _ in range(21)]
    reserved = [[False] * 21 for _ in range(21)]

    draw_finder(modules, reserved, 0, 0)
    draw_finder(modules, reserved, 0, 14)
    draw_finder(modules, reserved, 14, 0)

    for i in range(8, 13):
        modules[6][i] = i % 2 == 0
        reserved[6][i] = True
        modules[i][6] = i % 2 == 0
        reserved[i][6] = True

    reserve_format_areas(reserved)
    reserved[13][8] = True

    data = make_data_codewords(text)
    codewords = data + reed_solomon_remainder(data, 7)
    bits = []
    for codeword in codewords:
        bits += bits_from_int(codeword, 8)

    base = [row[:] for row in modules]
    bit_index = 0
    upward = True
    col = 20
    while col > 0:
        if col == 6:
            col -= 1
        rows = range(20, -1, -1) if upward else range(21)
        for row in rows:
            for c in (col, col - 1):
                if reserved[row][c]:
                    continue
                base[row][c] = bool(bits[bit_index]) if bit_index < len(bits) else False
                bit_index += 1
        upward = not upward
        col -= 2

    best = None
    best_score = None
    for mask in range(8):
        candidate = [row[:] for row in base]
        for row in range(21):
            for col in range(21):
                if not reserved[row][col] and mask_bit(mask, row, col):
                    candidate[row][col] = not candidate[row][col]
        add_format_bits(candidate, mask)
        score = penalty_score(candidate)
        if best_score is None or score < best_score:
            best = candidate
            best_score = score
    return best


def pdf_escape(text):
    return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


class Pdf:
    def __init__(self):
        self.objects = []

    def add(self, body):
        self.objects.append(body.encode("latin-1"))
        return len(self.objects)

    def save(self, path, root_id):
        output = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
        offsets = [0]
        for index, obj in enumerate(self.objects, start=1):
            offsets.append(len(output))
            output.extend(f"{index} 0 obj\n".encode("latin-1"))
            output.extend(obj)
            output.extend(b"\nendobj\n")
        startxref = len(output)
        output.extend(f"xref\n0 {len(self.objects) + 1}\n".encode("latin-1"))
        output.extend(b"0000000000 65535 f \n")
        for offset in offsets[1:]:
            output.extend(f"{offset:010d} 00000 n \n".encode("latin-1"))
        output.extend(
            f"trailer\n<< /Size {len(self.objects) + 1} /Root {root_id} 0 R >>\n"
            f"startxref\n{startxref}\n%%EOF\n".encode("latin-1")
        )
        path.write_bytes(output)


def make_pdf(output_path):
    width = 595.28
    height = 841.89
    margin_x = 42
    margin_y = 50
    cell_w = (width - 2 * margin_x) / 3
    cell_h = (height - 2 * margin_y) / 3
    qr_size = 118
    module = qr_size / 29  # 21 modules plus quiet zone on all sides.

    fixed_nodes = [f"C{i}" for i in range(1, 10)]

    commands = [
        "1 1 1 rg 0 0 595.28 841.89 re f",
        "0 0 0 rg",
        "BT /F1 16 Tf 72 805 Td (9 Fixed Delivery Node QR Codes) Tj ET",
        "BT /F1 9 Tf 72 789 Td (Scan in scan.html, confirm Yes, then the car starts delivery to C1 through C9.) Tj ET",
    ]

    for i, label in enumerate(fixed_nodes):
        matrix = make_qr_matrix(label)
        cell_col = i % 3
        cell_row = i // 3
        left = margin_x + cell_col * cell_w + (cell_w - qr_size) / 2
        top = height - margin_y - cell_row * cell_h - 28
        qr_bottom = top - qr_size

        commands.append("0 0 0 RG 0.5 w")
        commands.append(f"{left - 10:.2f} {qr_bottom - 30:.2f} {qr_size + 20:.2f} {qr_size + 54:.2f} re S")
        commands.append("0 0 0 rg")
        for row in range(29):
            for col in range(29):
                dark = False
                if 4 <= row < 25 and 4 <= col < 25:
                    dark = matrix[row - 4][col - 4]
                if dark:
                    x = left + col * module
                    y = qr_bottom + (28 - row) * module
                    commands.append(f"{x:.2f} {y:.2f} {module + 0.01:.2f} {module + 0.01:.2f} re f")

        commands.append(f"BT /F1 11 Tf {left + 24:.2f} {qr_bottom - 18:.2f} Td ({pdf_escape(label)}) Tj ET")

    stream = "\n".join(commands).encode("latin-1")

    pdf = Pdf()
    font_id = pdf.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    content_id = pdf.add(f"<< /Length {len(stream)} >>\nstream\n{stream.decode('latin-1')}\nendstream")
    page_id = pdf.add(
        f"<< /Type /Page /Parent 4 0 R /MediaBox [0 0 {width} {height}] "
        f"/Resources << /Font << /F1 {font_id} 0 R >> >> /Contents {content_id} 0 R >>"
    )
    pages_id = pdf.add(f"<< /Type /Pages /Kids [{page_id} 0 R] /Count 1 >>")
    catalog_id = pdf.add(f"<< /Type /Catalog /Pages {pages_id} 0 R >>")
    pdf.save(output_path, catalog_id)


if __name__ == "__main__":
    init_gf()
    target = Path(__file__).resolve().parents[1] / "qr_fixed_nodes_C1_C9.pdf"
    make_pdf(target)
    print(target)

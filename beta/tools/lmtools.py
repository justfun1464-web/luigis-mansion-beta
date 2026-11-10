# Luigi's Mansion BETA: file format helpers (Yay0/Yaz0, RARC, JMP tables, BTI textures).
# Written for this mod; mirrors the port's own readers (src/recomp/rarc.cpp) and the decomp's
# ToolData (JMP). No game data is included here.
import struct

# ---------------------------------------------------------------------------------------------
# Yay0 / Yaz0


def yay0_decompress(src: bytes) -> bytes:
    assert src[:4] == b"Yay0"
    size, link_off, chunk_off = struct.unpack(">III", src[4:16])
    mask_off = 16
    out = bytearray()
    mask = 0
    bits = 0
    while len(out) < size:
        if bits == 0:
            mask = struct.unpack(">I", src[mask_off:mask_off + 4])[0]
            mask_off += 4
            bits = 32
        if mask & 0x80000000:
            out.append(src[chunk_off])
            chunk_off += 1
        else:
            link = (src[link_off] << 8) | src[link_off + 1]
            link_off += 2
            dist = (link & 0xFFF) + 1
            n = link >> 12
            if n == 0:
                n = src[chunk_off] + 0x12
                chunk_off += 1
            else:
                n += 2
            start = len(out) - dist
            for i in range(n):
                out.append(out[start + i])
        mask = (mask << 1) & 0xFFFFFFFF
        bits -= 1
    return bytes(out)


def yaz0_decompress(src: bytes) -> bytes:
    assert src[:4] == b"Yaz0"
    size = struct.unpack(">I", src[4:8])[0]
    p = 16
    out = bytearray()
    while len(out) < size:
        code = src[p]
        p += 1
        for b in range(7, -1, -1):
            if len(out) >= size:
                break
            if code & (1 << b):
                out.append(src[p])
                p += 1
            else:
                b1, b2 = src[p], src[p + 1]
                p += 2
                dist = ((b1 & 0x0F) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0:
                    n = src[p] + 0x12
                    p += 1
                else:
                    n += 2
                start = len(out) - dist
                for i in range(n):
                    out.append(out[start + i])
    return bytes(out)


def decompress(data: bytes) -> bytes:
    if data[:4] == b"Yay0":
        return yay0_decompress(data)
    if data[:4] == b"Yaz0":
        return yaz0_decompress(data)
    return data


def yay0_compress(data: bytes) -> bytes:
    """LZ compression into a Yay0 stream (greedy, hash chains, window 4096, length 3..273)."""
    n = len(data)
    masks = []
    links = bytearray()
    chunks = bytearray()
    cur_mask = 0
    cur_bits = 0
    head = {}
    prev = [-1] * n
    MAX_CHAIN = 48

    def push_bit(bit):
        nonlocal cur_mask, cur_bits
        cur_mask = (cur_mask << 1) | bit
        cur_bits += 1
        if cur_bits == 32:
            masks.append(cur_mask)
            cur_mask = 0
            cur_bits = 0

    def insert(pos):
        if pos + 3 <= n:
            key = data[pos:pos + 3]
            prev[pos] = head.get(key, -1)
            head[key] = pos

    i = 0
    while i < n:
        best_len = 0
        best_dist = 0
        if i + 3 <= n:
            cand = head.get(data[i:i + 3], -1)
            chain = 0
            max_len = min(273, n - i)
            while cand >= 0 and chain < MAX_CHAIN:
                dist = i - cand
                if dist > 4096:
                    break
                l = 3
                while l < max_len and data[cand + l] == data[i + l]:
                    l += 1
                if l > best_len:
                    best_len = l
                    best_dist = dist
                    if l == max_len:
                        break
                cand = prev[cand]
                chain += 1
        if best_len >= 3:
            push_bit(0)
            d = best_dist - 1
            if best_len >= 0x12:
                links += struct.pack(">H", d)
                chunks.append(best_len - 0x12)
            else:
                links += struct.pack(">H", ((best_len - 2) << 12) | d)
            for k in range(best_len):
                insert(i + k)
            i += best_len
        else:
            push_bit(1)
            chunks.append(data[i])
            insert(i)
            i += 1
    if cur_bits:
        masks.append(cur_mask << (32 - cur_bits))
    mask_bytes = b"".join(struct.pack(">I", m) for m in masks)
    link_off = 16 + len(mask_bytes)
    chunk_off = link_off + len(links)
    out = b"Yay0" + struct.pack(">III", n, link_off, chunk_off) + mask_bytes + bytes(links) + bytes(chunks)
    return out


# ---------------------------------------------------------------------------------------------
# RARC


def _name_hash(s: str) -> int:
    h = 0
    for ch in s.encode("shift_jis", errors="replace"):
        h = (h * 3 + ch) & 0xFFFF
    return h


class Item:
    def __init__(self, name, dir=False, data=b"", node=-1, flags=0x11, id=0xFFFF):
        self.name = name
        self.dir = dir
        self.data = data
        self.node = node
        self.flags = flags
        self.id = id


class Archive:
    def __init__(self):
        self.nodes = []  # list of [name, [Item]]
        self.next_file_id = 0

    @staticmethod
    def parse(d: bytes) -> "Archive":
        d = decompress(d)
        assert d[:4] == b"RARC", "not a RARC archive"
        a = Archive()
        info = 0x20
        num_nodes, node_off, num_entries, entry_off, str_size, str_off = struct.unpack(">IIIIII", d[info:info + 24])
        node_off += info
        entry_off += info
        str_off += info
        a.next_file_id = struct.unpack(">H", d[info + 24:info + 26])[0]
        data_off = struct.unpack(">I", d[0x0C:0x10])[0] + 0x20

        def string(off):
            e = d.index(b"\0", str_off + off)
            return d[str_off + off:e].decode("shift_jis", errors="replace")

        for nn in range(num_nodes):
            no = node_off + nn * 16
            name = string(struct.unpack(">I", d[no + 4:no + 8])[0])
            count = struct.unpack(">H", d[no + 10:no + 12])[0]
            first = struct.unpack(">I", d[no + 12:no + 16])[0]
            items = []
            for i in range(first, first + count):
                e = entry_off + i * 20
                fid, _h, flags_name, aa, size = struct.unpack(">HHIII", d[e:e + 16])
                nm = string(flags_name & 0xFFFFFF)
                if nm in (".", ".."):
                    continue
                flags = flags_name >> 24
                if flags & 0x02:
                    items.append(Item(nm, True, node=aa, flags=flags))
                else:
                    items.append(Item(nm, False, d[data_off + aa:data_off + aa + size], flags=flags, id=fid))
            a.nodes.append([name, items])
        return a

    def build(self) -> bytes:
        order = [0]
        new_index = [-1] * len(self.nodes)
        parent = [-1] * len(self.nodes)
        new_index[0] = 0
        i = 0
        while i < len(order):
            for it in self.nodes[order[i]][1]:
                if it.dir and it.node >= 0 and new_index[it.node] < 0:
                    new_index[it.node] = len(order)
                    parent[it.node] = order[i]
                    order.append(it.node)
            i += 1
        strings = bytearray()
        str_index = {}

        def add_str(s):
            if s in str_index:
                return str_index[s]
            off = len(strings)
            strings.extend(s.encode("shift_jis") + b"\0")
            str_index[s] = off
            return off

        add_str(".")
        add_str("..")
        num_entries = sum(len(self.nodes[n][1]) + 2 for n in order)
        node_tab = bytearray(len(order) * 16)
        entry_tab = bytearray(num_entries * 20)
        data = bytearray()
        e = 0
        file_id = 0
        for ni, n in enumerate(order):
            name, items = self.nodes[n]
            no = ni * 16
            if ni == 0:
                node_tab[no:no + 4] = b"ROOT"
            else:
                t = name.upper().encode("shift_jis")[:4]
                node_tab[no:no + 4] = t + b" " * (4 - len(t))
            struct.pack_into(">IHHI", node_tab, no + 4, add_str(name), _name_hash(name), len(items) + 2, e)

            def entry(fid, nm, flags, aa, size):
                nonlocal e
                eo = e * 20
                e += 1
                struct.pack_into(">HHIII", entry_tab, eo, fid, _name_hash(nm), (flags << 24) | add_str(nm), aa, size)

            for it in items:
                if it.dir:
                    entry(0xFFFF, it.name, 0x02, new_index[it.node], 0x10)
                else:
                    at = len(data)
                    data.extend(it.data)
                    data.extend(b"\0" * ((-len(data)) % 32))
                    entry(it.id if it.id != 0xFFFF else file_id, it.name, it.flags, at, len(it.data))
                file_id += 1
            up = parent[n]
            entry(0xFFFF, ".", 0x02, ni, 0x10)
            entry(0xFFFF, "..", 0x02, 0xFFFFFFFF if up < 0 else new_index[up], 0x10)
        strings.extend(b"\0" * ((-len(strings)) % 32))
        out = bytearray(0x40)
        out += node_tab
        out += b"\0" * ((-len(out)) % 32)
        entry_off = len(out) - 0x20
        out += entry_tab
        out += b"\0" * ((-len(out)) % 32)
        str_off = len(out) - 0x20
        out += strings
        out += b"\0" * ((-len(out)) % 32)
        data_off = len(out) - 0x20
        out += data
        out[0:4] = b"RARC"
        struct.pack_into(">IIIII", out, 4, len(out), 0x20, data_off, len(data), len(data))
        struct.pack_into(">IIIIII", out, 0x20, len(order), 0x20, num_entries, entry_off, len(strings), str_off)
        struct.pack_into(">H", out, 0x38, max(self.next_file_id, file_id))
        return bytes(out)

    def _walk(self, node, prefix):
        for it in self.nodes[node][1]:
            p = prefix + it.name
            if it.dir:
                yield from self._walk(it.node, p + "/")
            else:
                yield p, it

    def files(self):
        return list(self._walk(0, ""))

    def find(self, path):
        node = 0
        parts = path.split("/")
        for k, part in enumerate(parts):
            found = None
            for it in self.nodes[node][1]:
                if it.name.lower() == part.lower():
                    found = it
            if found is None:
                return None
            if k == len(parts) - 1:
                return found
            if not found.dir:
                return None
            node = found.node
        return None

    def get(self, path) -> bytes:
        it = self.find(path)
        return None if it is None else it.data

    def put(self, path, data: bytes):
        node = 0
        parts = path.split("/")
        for k, part in enumerate(parts):
            found = None
            for it in self.nodes[node][1]:
                if it.name.lower() == part.lower():
                    found = it
            if k == len(parts) - 1:
                if found is not None and not found.dir:
                    found.data = data
                else:
                    self.nodes[node][1].append(Item(part, False, data))
                return
            if found is None:
                self.nodes.append([part, []])
                it = Item(part, True, node=len(self.nodes) - 1, flags=0x02)
                self.nodes[node][1].append(it)
                node = len(self.nodes) - 1
            else:
                node = found.node


# ---------------------------------------------------------------------------------------------
# JMP tables (Koga::ToolData)


def jmp_hash(name: str) -> int:
    h = 0
    for ch in name.encode("shift_jis"):
        h = (ch + (h << 8)) % 0x1FFFFD9
    return h


KNOWN_FIELDS = """name create_name path_name codename character_name pos_x pos_y pos_z dir_x dir_y dir_z
scale_x scale_y scale_z room_no event_no item_table appear_point msg_no cond_type move_type appear_type
cond_arg0 cond_arg1 cond_arg2 do_type string_arg0 string_arg1 int_arg0 int_arg1 int_arg2 int_arg3
EventFlag EventNo EventIf EventLock EventArea EventLoad disappear_flag appear_flag invisible
EnemyActType furniture_name furniture_type room_no generate_num next_fast_wait generate_wait
open_type door_type door_model DoorType item_name itemappear_flag telesa_hp flag_id loop
pos_x pos_y pos_z CameraNo""".split()


class Jmp:
    def __init__(self, raw: bytes):
        self.n_entries, self.n_fields, self.data_off, self.entry_size = struct.unpack(">iiiI", raw[:16])
        self.fields = []  # (hash, mask, offset, shift, type)
        for i in range(self.n_fields):
            o = 16 + i * 12
            h, m, off, sh, ty = struct.unpack(">IIHBB", raw[o:o + 12])
            self.fields.append([h, m, off, sh, ty])
        self.header = raw[:self.data_off]
        self.rows = [bytearray(raw[self.data_off + i * self.entry_size:self.data_off + (i + 1) * self.entry_size])
                     for i in range(self.n_entries)]
        self.tail = raw[self.data_off + self.n_entries * self.entry_size:]
        names = {jmp_hash(n): n for n in KNOWN_FIELDS}
        self.names = [names.get(f[0], "0x%08X" % f[0]) for f in self.fields]

    def field_index(self, name):
        h = jmp_hash(name)
        for i, f in enumerate(self.fields):
            if f[0] == h:
                return i
        return -1

    def get(self, row, name):
        i = self.field_index(name) if isinstance(name, str) else name
        if i < 0:
            return None
        h, m, off, sh, ty = self.fields[i]
        r = self.rows[row]
        if ty == 1:
            raw = bytes(r[off:off + 32])
            return raw.split(b"\0")[0].decode("shift_jis", errors="replace")
        if ty == 2:
            return struct.unpack(">f", r[off:off + 4])[0]
        v = struct.unpack(">I", r[off:off + 4])[0]
        v = (v & m) >> sh
        return v

    def set(self, row, name, value):
        i = self.field_index(name) if isinstance(name, str) else name
        assert i >= 0, name
        h, m, off, sh, ty = self.fields[i]
        r = self.rows[row]
        if ty == 1:
            b = value.encode("shift_jis")[:31]
            r[off:off + 32] = b + b"\0" * (32 - len(b))
        elif ty == 2:
            r[off:off + 4] = struct.pack(">f", float(value))
        else:
            old = struct.unpack(">I", r[off:off + 4])[0]
            v = (old & ~m & 0xFFFFFFFF) | ((int(value) << sh) & m)
            r[off:off + 4] = struct.pack(">I", v)

    def row_dict(self, row):
        return {self.names[i]: self.get(row, i) for i in range(self.n_fields)}

    def add_row(self, copy_of: int):
        self.rows.append(bytearray(self.rows[copy_of]))
        return len(self.rows) - 1

    def build(self) -> bytes:
        hdr = bytearray(self.header)
        struct.pack_into(">i", hdr, 0, len(self.rows))
        out = bytes(hdr) + b"".join(bytes(r) for r in self.rows)
        # tables are padded to 32 bytes with '@' in the game's files
        pad = (-len(out)) % 32
        return out + b"@" * pad


# ---------------------------------------------------------------------------------------------
# BTI (single texture) decode / encode, RGBA8 numpy arrays

def _rgb565(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2, 255)


def _rgb5a3(v):
    if v & 0x8000:
        r = (v >> 10) & 0x1F
        g = (v >> 5) & 0x1F
        b = v & 0x1F
        return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255)
    a = (v >> 12) & 7
    r = (v >> 8) & 0xF
    g = (v >> 4) & 0xF
    b = v & 0xF
    return (r * 17, g * 17, b * 17, a << 5 | a << 2 | a >> 1)


BLOCK = {0: (8, 8, 4), 1: (8, 4, 8), 2: (8, 4, 8), 3: (4, 4, 16), 4: (4, 4, 16), 5: (4, 4, 16),
         6: (4, 4, 32), 8: (8, 8, 4), 9: (8, 4, 8), 10: (4, 4, 16), 14: (8, 8, 4)}


def decode_texture(fmt, w, h, data, palette=None, pal_fmt=0):
    import numpy as np
    out = np.zeros((h, w, 4), np.uint8)
    bw, bh, bpp = BLOCK[fmt]
    bx_n = (w + bw - 1) // bw
    by_n = (h + bh - 1) // bh
    pos = 0

    def pal_color(idx):
        v = palette[idx]
        if pal_fmt == 0:  # IA8
            return (v & 0xFF, v & 0xFF, v & 0xFF, v >> 8)
        if pal_fmt == 1:
            return _rgb565(v)
        return _rgb5a3(v)

    for by in range(by_n):
        for bx in range(bx_n):
            if fmt == 14:  # CMPR: 2x2 sub-blocks of 4x4 DXT1
                for sb in range(4):
                    c0, c1 = struct.unpack(">HH", data[pos:pos + 4])
                    idx = struct.unpack(">I", data[pos + 4:pos + 8])[0]
                    pos += 8
                    p0 = _rgb565(c0)
                    p1 = _rgb565(c1)
                    if c0 > c1:
                        p2 = tuple((2 * a + b) // 3 for a, b in zip(p0, p1))
                        p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))
                    else:
                        p2 = tuple((a + b) // 2 for a, b in zip(p0, p1))
                        p3 = (0, 0, 0, 0)
                    pal = (p0, p1, p2, p3)
                    sx = bx * 8 + (sb & 1) * 4
                    sy = by * 8 + (sb >> 1) * 4
                    for py in range(4):
                        for px in range(4):
                            ci = (idx >> (30 - 2 * (py * 4 + px))) & 3
                            X, Y = sx + px, sy + py
                            if X < w and Y < h:
                                out[Y, X] = pal[ci]
                continue
            for py in range(bh):
                for px in range(bw):
                    X, Y = bx * bw + px, by * bh + py
                    if fmt == 6:  # RGBA8: AR then GB planes
                        k = py * 4 + px
                        a = data[pos + k * 2]
                        r = data[pos + k * 2 + 1]
                        g = data[pos + 32 + k * 2]
                        b = data[pos + 32 + k * 2 + 1]
                        col = (r, g, b, a)
                    elif fmt == 0:  # I4
                        k = py * 8 + px
                        byte = data[pos + k // 2]
                        v = (byte >> 4) if k % 2 == 0 else (byte & 0xF)
                        v *= 17
                        col = (v, v, v, v)
                    elif fmt == 1:  # I8
                        v = data[pos + py * 8 + px]
                        col = (v, v, v, v)
                    elif fmt == 2:  # IA4
                        byte = data[pos + py * 8 + px]
                        i = (byte & 0xF) * 17
                        a = (byte >> 4) * 17
                        col = (i, i, i, a)
                    elif fmt == 3:  # IA8
                        a = data[pos + (py * 4 + px) * 2]
                        i = data[pos + (py * 4 + px) * 2 + 1]
                        col = (i, i, i, a)
                    elif fmt == 4:
                        col = _rgb565(struct.unpack(">H", data[pos + (py * 4 + px) * 2:pos + (py * 4 + px) * 2 + 2])[0])
                    elif fmt == 5:
                        col = _rgb5a3(struct.unpack(">H", data[pos + (py * 4 + px) * 2:pos + (py * 4 + px) * 2 + 2])[0])
                    elif fmt == 8:  # C4
                        k = py * 8 + px
                        byte = data[pos + k // 2]
                        col = pal_color((byte >> 4) if k % 2 == 0 else (byte & 0xF))
                    elif fmt == 9:  # C8
                        col = pal_color(data[pos + py * 8 + px])
                    else:
                        col = (255, 0, 255, 255)
                    if X < w and Y < h:
                        out[Y, X] = col
            pos += 64 if fmt == 6 else 32
    return out


def read_bti(b: bytes, base=0):
    fmt = b[base]
    w, h = struct.unpack(">HH", b[base + 2:base + 6])
    pal_fmt = b[base + 9]
    n_pal = struct.unpack(">H", b[base + 0x0A:base + 0x0C])[0]
    pal_off = struct.unpack(">I", b[base + 0x0C:base + 0x10])[0]
    img_off = struct.unpack(">I", b[base + 0x1C:base + 0x20])[0]
    palette = None
    if fmt in (8, 9, 10) and n_pal:
        palette = [struct.unpack(">H", b[base + pal_off + 2 * i:base + pal_off + 2 * i + 2])[0] for i in range(n_pal)]
    return decode_texture(fmt, w, h, b[base + img_off:], palette, pal_fmt)


def encode_rgba8(img):
    """numpy HxWx4 uint8 -> GX RGBA8 texture data (w, h multiples of 4 padded)."""
    import numpy as np
    h, w = img.shape[:2]
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            ar = bytearray()
            gb = bytearray()
            for py in range(4):
                for px in range(4):
                    Y, X = by + py, bx + px
                    if Y < h and X < w:
                        r, g, b, a = (int(v) for v in img[Y, X])
                    else:
                        r = g = b = a = 0
                    ar += bytes((a, r))
                    gb += bytes((g, b))
            out += ar + gb
    return bytes(out)


def encode_rgb5a3(img):
    h, w = img.shape[:2]
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            for py in range(4):
                for px in range(4):
                    Y, X = by + py, bx + px
                    if Y < h and X < w:
                        r, g, b, a = (int(v) for v in img[Y, X])
                    else:
                        r = g = b = a = 0
                    if a >= 0xE0:
                        v = 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
                    else:
                        v = (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)
                    out += struct.pack(">H", v)
    return bytes(out)


def make_bti(img, fmt=6, wrap_s=0, wrap_t=0, min_filter=1, mag_filter=1):
    """Build a single-image BTI (RGBA8 or RGB5A3) from an HxWx4 array."""
    h, w = img.shape[:2]
    data = encode_rgba8(img) if fmt == 6 else encode_rgb5a3(img)
    hdr = bytearray(0x20)
    hdr[0] = fmt
    hdr[1] = 0  # alpha
    struct.pack_into(">HH", hdr, 2, w, h)
    hdr[6] = wrap_s
    hdr[7] = wrap_t
    hdr[0x14] = min_filter
    hdr[0x15] = mag_filter
    hdr[0x18] = 1  # mipmap count
    struct.pack_into(">I", hdr, 0x1C, 0x20)
    return bytes(hdr) + data

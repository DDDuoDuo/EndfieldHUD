#!/usr/bin/env python3
"""Write the synthetic PDFs of the Reader PDF-title oracle into a NEW directory.

Each case is a small one-page PDF whose document information dictionary
exercises one part of how PDFKit (the Mac ReaderDocument) finds and decodes
/Title: string syntax, text encodings, indirect objects, incremental updates,
cross-reference streams with predictors, object streams, hybrid files, damage
and encryption. Deterministic bytes; nothing is read from the user.
Usage: reader_pdf_title_cases.py NEW-directory
"""
import hashlib
import os
from pathlib import Path
import sys
import zlib

HEADER = b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n"


def pages(start=1):
    catalog, tree, page = start, start + 1, start + 2
    return {catalog: b"<< /Type /Catalog /Pages %d 0 R >>" % tree,
            tree: b"<< /Type /Pages /Kids [%d 0 R] /Count 1 >>" % page,
            page: b"<< /Type /Page /Parent %d 0 R /MediaBox [0 0 200 200] >>" % tree}, catalog


def classic(objects, root, info, trailer=b"", header=HEADER):
    data = bytearray(header)
    offsets = {}
    for n in sorted(objects):
        offsets[n] = len(data)
        data += b"%d 0 obj\n" % n + objects[n] + b"\nendobj\n"
    xref, size = len(data), max(objects) + 1
    data += b"xref\n0 %d\n0000000000 65535 f \n" % size
    for n in range(1, size):
        data += b"%010d 00000 n \n" % offsets[n] if n in offsets else b"0000000000 65535 f \n"
    data += b"trailer\n<< /Size %d /Root %d 0 R" % (size, root)
    if info:
        data += b" /Info %d 0 R" % info
    data += trailer + b" >>\nstartxref\n%d\n%%%%EOF\n" % xref
    return bytes(data)


def titled(token, info_extra=b""):
    objects, root = pages()
    objects[4] = b"<< /Title " + token + info_extra + b" /Producer (synthetic) >>"
    return classic(objects, root, 4)


def info_dictionary(dictionary):
    objects, root = pages()
    objects[4] = dictionary
    return classic(objects, root, 4)


def update(base, body, trailer):
    """Appends one incremental revision (object bodies by number)."""
    previous = int(base.rsplit(b"startxref\n", 1)[1].split(b"\n")[0])
    data = bytearray(base)
    offsets = {}
    for n, value in body.items():
        offsets[n] = len(data)
        data += b"%d 0 obj\n" % n + value + b"\nendobj\n"
    xref = len(data)
    data += b"xref\n0 1\n0000000000 65535 f \n"
    for n, entry in sorted(trailer["entries"].items()):
        data += b"%d 1\n" % n + (b"%010d 00000 n \n" % offsets[n] if entry == "n" else b"0000000000 00001 f \n")
    data += b"trailer\n<< " + trailer["dict"] + b" /Prev %d >>\nstartxref\n%d\n%%%%EOF\n" % (previous, xref)
    return bytes(data)


def png_up(rows, columns):
    previous, out = bytes(columns), b""
    for row in rows:
        out += b"\x02" + bytes((row[j] - previous[j]) & 0xFF for j in range(columns))
        previous = row
    return out


def png_mixed(rows, columns, filters):
    previous, out = bytes(columns), b""
    for i, row in enumerate(rows):
        f = filters[i % len(filters)]
        encoded = bytearray()
        for j in range(columns):
            left, up, corner = (row[j - 1] if j else 0), previous[j], (previous[j - 1] if j else 0)
            if f == 0: predicted = 0
            elif f == 1: predicted = left
            elif f == 2: predicted = up
            elif f == 3: predicted = (left + up) // 2
            else:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                predicted = left if pa <= pb and pa <= pc else up if pb <= pc else corner
            encoded.append((row[j] - predicted) & 0xFF)
        out += bytes([f]) + bytes(encoded)
        previous = row
    return out


def xref_stream(objects, root, info, compressed=()):
    """Cross-reference stream (W [1 4 2], PNG Up) with optional object stream."""
    data = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n")
    offsets, entries = {}, {}
    for n in sorted(objects):
        if n in compressed:
            continue
        offsets[n] = len(data)
        data += b"%d 0 obj\n" % n + objects[n] + b"\nendobj\n"
    if compressed:
        holder = max(objects) + 1
        header = b"".join(b"%d %d " % (n, sum(len(objects[m]) + 1 for m in compressed[:i])) for i, n in enumerate(compressed))
        body = b"".join(objects[n] + b" " for n in compressed)
        packed = zlib.compress(header + body)
        offsets[holder] = len(data)
        data += b"%d 0 obj\n<< /Type /ObjStm /N %d /First %d /Filter /FlateDecode /Length %d >>\nstream\n" % (holder, len(compressed), len(header), len(packed)) + packed + b"\nendstream\nendobj\n"
        for i, n in enumerate(compressed):
            entries[n] = (2, holder, i)
    number = max(list(objects) + list(offsets)) + 1
    offsets[number] = len(data)
    for n, o in offsets.items():
        entries[n] = (1, o, 0)
    size = number + 1
    rows = [bytes([entries.get(n, (0, 0, 0))[0]]) + entries.get(n, (0, 0, 0))[1].to_bytes(4, "big") +
            (65535 if n == 0 else entries.get(n, (0, 0, 0))[2]).to_bytes(2, "big") for n in range(size)]
    packed = zlib.compress(png_up(rows, 7))
    data += b"%d 0 obj\n<< /Type /XRef /Size %d /W [1 4 2] /Root %d 0 R /Info %d 0 R /Filter /FlateDecode /DecodeParms << /Columns 7 /Predictor 12 >> /Length %d >>\nstream\n" % (
        number, size, root, info, len(packed)) + packed + b"\nendstream\nendobj\nstartxref\n%d\n%%%%EOF\n" % offsets[number]
    return bytes(data)


def predictors():
    """Index subsections, W [0 3 1] (implicit type 1) and every PNG row filter."""
    data = bytearray(b"%PDF-1.5\n")
    objects, root = pages()
    objects[4] = b"<< /Title (Predictor Rows) >>"
    offsets = {}
    for n in sorted(objects):
        offsets[n] = len(data)
        data += b"%d 0 obj\n" % n + objects[n] + b"\nendobj\n"
    offsets[5] = len(data)
    rows = [(0).to_bytes(3, "big") + b"\x00"] + [offsets[n].to_bytes(3, "big") + b"\x00" for n in range(1, 6)]
    packed = zlib.compress(png_mixed(rows, 4, [0, 1, 2, 3, 4, 2]))
    data += b"5 0 obj\n<< /Type /XRef /Size 6 /Index [0 1 1 5] /W [0 3 1] /Root 1 0 R /Info 4 0 R /Filter [/FlateDecode] /DecodeParms [<< /Predictor 15 /Columns 4 >>] /Length %d >>\nstream\r\n" % len(
        packed) + packed + b"\nendstream\nendobj\nstartxref\n%d\n%%%%EOF\n" % offsets[5]
    return bytes(data)


def hybrid():
    """Classic table marking the information dictionary free, listed in /XRefStm."""
    data = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n")
    objects, root = pages()
    offsets = {}
    for n in sorted(objects):
        offsets[n] = len(data)
        data += b"%d 0 obj\n" % n + objects[n] + b"\nendobj\n"
    header = b"4 0 "
    packed = zlib.compress(header + b"<< /Title (Hybrid Hidden) >>")
    offsets[5] = len(data)
    data += b"5 0 obj\n<< /Type /ObjStm /N 1 /First %d /Filter /FlateDecode /Length 7 0 R >>\nstream\n" % len(header) + packed + b"\nendstream\nendobj\n"
    offsets[7] = len(data)
    data += b"7 0 obj\n%d\nendobj\n" % len(packed)
    hidden = len(data)
    rows = b""
    for n in range(8):
        if n == 4: rows += b"\x02" + (5).to_bytes(4, "big") + (0).to_bytes(2, "big")
        elif n == 6: rows += b"\x01" + hidden.to_bytes(4, "big") + (0).to_bytes(2, "big")
        elif n in offsets: rows += b"\x01" + offsets[n].to_bytes(4, "big") + (0).to_bytes(2, "big")
        else: rows += b"\x00" + (0).to_bytes(4, "big") + (65535 if n == 0 else 0).to_bytes(2, "big")
    data += b"6 0 obj\n<< /Type /XRef /Size 8 /W [1 4 2] /Index [0 8] /Length %d >>\nstream\n" % len(rows) + rows + b"\nendstream\nendobj\n"
    xref = len(data)
    data += b"xref\n0 8\n0000000000 65535 f \n"
    for n in range(1, 8):
        data += b"%010d 00000 n \n" % (hidden if n == 6 else offsets[n]) if n != 4 else b"0000000000 00001 f \n"
    data += b"trailer\n<< /Size 8 /Root 1 0 R /Info 4 0 R /XRefStm %d >>\nstartxref\n%d\n%%%%EOF\n" % (hidden, xref)
    return bytes(data)


def rc4(key, data):
    s, j = list(range(256)), 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) % 256
        s[i], s[j] = s[j], s[i]
    i = j = 0
    out = bytearray()
    for b in data:
        i = (i + 1) % 256
        j = (j + s[i]) % 256
        s[i], s[j] = s[j], s[i]
        out.append(b ^ s[(s[i] + s[j]) % 256])
    return bytes(out)


def encrypted_rc4():
    """Standard security handler R2 with an owner password only (opens unlocked)."""
    padding = bytes.fromhex("28BF4E5E4E758A4164004E56FFFA01082E2E00B6D0683E802F0CA9FE6453697A")
    pad = lambda p: (p + padding)[:32]
    permissions, identifier = -44 & 0xFFFFFFFF, bytes(range(16))
    owner = rc4(hashlib.md5(pad(b"owner")).digest()[:5], pad(b""))
    key = hashlib.md5(pad(b"") + owner + permissions.to_bytes(4, "little") + identifier).digest()[:5]
    user = rc4(key, padding)
    object_key = hashlib.md5(key + (4).to_bytes(3, "little") + (0).to_bytes(2, "little")).digest()[:10]
    objects, root = pages()
    objects[4] = b"<< /Title <" + rc4(object_key, b"Secret Title").hex().encode() + b"> >>"
    objects[5] = b"<< /Filter /Standard /V 1 /R 2 /O <" + owner.hex().encode() + b"> /U <" + user.hex().encode() + b"> /P -44 >>"
    ident = identifier.hex().encode()
    return classic(objects, root, 4, b" /Encrypt 5 0 R /ID [<" + ident + b"><" + ident + b">]")


def cases():
    out = {}
    out["plain"] = titled(b"(Plain Title)")
    out["escapes"] = titled(b"(Paren \\( \\) back\\\\ tab\\tend \\101\\102\\0103 cont\\\nline (nested (deep)) x)")
    out["escape_controls"] = titled(b"(a\\rb\\nc\\bd\\fe)")
    out["crlf_continuation"] = titled(b"(cont\\\r\nnued)")
    out["unknown_escape"] = titled(b"(a\\qb\\8c\\9)")
    out["octal_overflow"] = titled(b"(\\777x)")
    out["raw_line_ends"] = titled(b"(Line1\r\nLine2\rLine3)")
    out["all_bytes"] = titled(b"(" + b"".join(b"\\%03o" % c for c in range(256)) + b")")
    out["pdfdoc"] = titled(b"(Bullet \x80 Euro \xa0 e\xe9 dagger \x81 quote \x8d\x8e ligature \x93)")
    out["utf8_bom"] = titled(b"(\xef\xbb\xbf" + "UTF-8 标题".encode() + b")")
    out["hex_utf16"] = titled(b"<" + "﻿中文标题😀".encode("utf-16-be").hex().encode() + b">")
    out["hex_utf16_japanese"] = titled(b"<FEFF" + "電子書籍のタイトル".encode("utf-16-be").hex().upper().encode() + b">")
    out["literal_utf16"] = titled(b"(\xfe\xff" + "Unicode – 標題".encode("utf-16-be").replace(b"\\", b"\\\\").replace(b"(", b"\\(").replace(b")", b"\\)") + b")")
    out["utf16le_bom"] = titled(b"<" + "﻿Little".encode("utf-16-le").hex().encode() + b">")
    out["utf16le_surrogate"] = titled(b"<FFFE3DD800DE4100>")
    out["utf16_odd"] = titled(b"<FEFF004100>")
    out["utf16_unpaired"] = titled(b"<FEFFD800004100>")
    out["utf16_nul"] = titled(b"<FEFF0041000000420000>")
    out["utf16_bom_only"] = titled(b"<FEFF>")
    out["hex_plain"] = titled(b"<48656C6C6F2057 6F726C64>")
    out["hex_whitespace"] = titled(b"<4 8\n65 6c\t6C 6F>")
    out["hex_odd"] = titled(b"<41424>")
    out["empty"] = titled(b"()")
    out["spaces"] = titled(b"(   )")
    out["padded"] = titled(b"(  padded title  )")
    out["nul_end"] = titled(b"(Title\\000)")
    out["number_title"] = titled(b"42")
    out["name_title"] = titled(b"/NameTitle")
    out["long_title"] = titled(b"(" + b"Long title " * 120 + b")")
    out["missing"] = classic(*pages(), None)
    out["escaped_key"] = info_dictionary(b"<< /T#69tle (Escaped Key) >>")
    out["duplicate_key"] = info_dictionary(b"<< /Title (First Dup) /Title (Second Dup) >>")
    out["nested"] = info_dictionary(b"<< /Custom << /Title (Nested) /Arr [ (x) <41> << /a 1 >> ] >> /Keywords [1 2 (3)] /Title (Outer) >>")
    out["tight"] = info_dictionary(b"<</Title(Tight)/Author(a)>>")
    out["comment"] = info_dictionary(b"<< % comment /Title (No)\n /Title (Comment Skipped) >>")
    objects, root = pages()
    objects[4], objects[5] = b"<< /Title 5 0 R >>", b"(Indirect Title)"
    out["indirect"] = classic(objects, root, 4)
    objects, root = pages()
    objects[4], objects[5], objects[6] = b"<< /Title 5 0 R >>", b"6 0 R", b"(Double Indirect)"
    out["double_indirect"] = classic(objects, root, 4)
    objects, root = pages()
    objects[4] = b"<< /Title (Generation One) >>"
    data = classic(objects, root, 4).replace(b"4 0 obj", b"4 1 obj").replace(b"/Info 4 0 R", b"/Info 4 1 R")
    start = data.index(b"xref\n")
    lines = data[start:].split(b"\n")
    lines[2 + 4] = lines[2 + 4][:11] + b"00001" + lines[2 + 4][16:]
    out["generation"] = data[:start] + b"\n".join(lines)
    objects, root = pages()
    out["direct_info"] = classic(objects, root, None, b" /Info << /Title (Direct Dict) >>")
    objects, root = pages()
    objects[4] = b"<< /Title (After Junk) >>"
    out["junk_header"] = b"JUNKJUNKJUNK\n" + classic(objects, root, 4)
    objects[4] = b"<< /Title (Trailing Garbage) >>"
    out["trailing_garbage"] = classic(objects, root, 4) + b"\n" + b"x" * 2000 + b"\n"
    objects[4] = b"<< /Title (First Revision) >>"
    base = classic(objects, root, 4)
    out["incremental"] = update(base, {4: b"<< /Title (Second Revision) >>"}, {"entries": {4: "n"}, "dict": b"/Size 5 /Root 1 0 R /Info 4 0 R"})
    out["incremental_new_info"] = update(base, {6: b"<< /Title (New Info Object) >>"}, {"entries": {6: "n"}, "dict": b"/Size 7 /Root 1 0 R /Info 6 0 R"})
    out["info_old_revision"] = update(base, {5: b"(unrelated)"}, {"entries": {5: "n"}, "dict": b"/Size 6 /Root 1 0 R"})
    out["info_deleted"] = update(base, {}, {"entries": {4: "f"}, "dict": b"/Size 5 /Root 1 0 R /Info 4 0 R"})
    objects, root = pages()
    objects[4] = b"<< /Title (Xref Stream Title) >>"
    out["xref_stream"] = xref_stream(objects, root, 4)
    objects[4] = b"<< /Title <FEFF00530074007200650061006D> /Author (x) >>"
    out["object_stream"] = xref_stream(objects, root, 4, (4,))
    out["predictors"] = predictors()
    out["hybrid"] = hybrid()
    objects, root = pages()
    objects[4] = b"<< /Title (Broken Offset) >>"
    out["broken_startxref"] = classic(objects, root, 4).replace(b"startxref\n", b"startxref\n9")
    out["encrypted_rc4"] = encrypted_rc4()
    return out


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    target = Path(sys.argv[1])
    if target.exists() and any(target.iterdir()):
        raise SystemExit("Choose a NEW empty directory")
    target.mkdir(parents=True, exist_ok=True)
    for name, data in cases().items():
        (target / (name + ".pdf")).write_bytes(data)


if __name__ == "__main__":
    main()

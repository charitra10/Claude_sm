#!/usr/bin/env python3
"""replay.py: decode unswbc .replay files (packed Cap'n Proto, schema taken from the VS Code viewer).

    from replay import load
    rp = load(path)   # rp.map_text, rp.bot_a, rp.bot_b, rp.events (list of tuples), rp.result
Event tuples: ('round', r) ('turn', id) ('countdown', x, y, c) ('tile', x, y, has_pearl) ('action', id, kind, arg)
('log', id, text) ('ind', id, text) ('update', id, facing, hx, hy, tx, ty) ('split', parent, child, team, facing,
parent_body, child_body) ('death', id, reason) ('sonar', sender, dir, value, ox, oy, ex, ey, hit_id, hit_kind) ('elog', id, text)
"""
import struct, sys

DIRS = 'NESW'
DEATH = ['hit wall', 'hit self', 'hit other body', 'head to head', 'no valid action']


def unpack(data):
    out = bytearray(); i = 0; n = len(data)
    while i < n:
        tag = data[i]; i += 1
        if tag == 0:
            cnt = data[i]; i += 1
            out += bytes(8 * (cnt + 1))
        elif tag == 0xFF:
            out += data[i:i + 8]; i += 8
            cnt = data[i]; i += 1
            out += data[i:i + 8 * cnt]; i += 8 * cnt
        else:
            word = bytearray(8)
            for b in range(8):
                if tag & (1 << b):
                    word[b] = data[i]; i += 1
            out += word
    return bytes(out)


class Msg:
    def __init__(self, raw):
        nseg = struct.unpack_from('<I', raw, 0)[0] + 1
        sizes = struct.unpack_from('<%dI' % nseg, raw, 4)
        off = (4 + 4 * nseg + 7) & ~7
        self.segs = []
        for s in sizes:
            self.segs.append(raw[off:off + 8 * s]); off += 8 * s

    def word(self, seg, w):
        return struct.unpack_from('<Q', self.segs[seg], 8 * w)[0]

    def resolve(self, seg, w):
        """Pointer at (seg, word w) -> (seg, pointer word position, pointer value) after far pointers."""
        p = self.word(seg, w)
        if p & 3 == 2:
            double = (p >> 2) & 1; off = (p >> 3) & 0x1FFFFFFF; tseg = p >> 32
            if not double:
                return tseg, off, self.word(tseg, off)
            land = self.word(tseg, off); tag = self.word(tseg, off + 1)
            tseg2 = land >> 32; off2 = (land >> 3) & 0x1FFFFFFF
            return tseg2, off2 - 1 - ((tag >> 2) & 0x3FFFFFFF if False else 0), tag | (0), 'double', off2
        return seg, w, p

    def struct_at(self, seg, w):
        r = self.resolve(seg, w)
        if len(r) == 5:  # double far: content at (tseg2, off2), tag describes it
            tseg2, _, tag, _, off2 = r
            if tag == 0: return None
            dsz = (tag >> 32) & 0xFFFF; psz = tag >> 48
            return S(self, tseg2, off2, dsz, psz)
        seg, w, p = r
        if p == 0: return None
        off = p >> 2 & 0x3FFFFFFF
        if off & 0x20000000: off -= 0x40000000
        dsz = (p >> 32) & 0xFFFF; psz = p >> 48
        return S(self, seg, w + 1 + off, dsz, psz)

    def list_at(self, seg, w):
        r = self.resolve(seg, w)
        if len(r) == 5:
            tseg2, _, tag, _, off2 = r
            p = tag; start = off2; seg = tseg2
        else:
            seg, w, p = r
            if p == 0: return None
            off = p >> 2 & 0x3FFFFFFF
            if off & 0x20000000: off -= 0x40000000
            start = w + 1 + off
        esz = (p >> 32) & 7; cnt = p >> 35
        return seg, start, esz, cnt


class S:
    __slots__ = ('m', 'seg', 'w', 'dsz', 'psz')

    def __init__(self, m, seg, w, dsz, psz):
        self.m, self.seg, self.w, self.dsz, self.psz = m, seg, w, dsz, psz

    def _d(self, fmt, off):
        if off + struct.calcsize(fmt) > 8 * self.dsz: return 0
        return struct.unpack_from(fmt, self.m.segs[self.seg], 8 * self.w + off)[0]

    def i32(self, off): return self._d('<i', off)
    def u16(self, off): return self._d('<H', off)
    def u32(self, off): return self._d('<I', off)
    def u64(self, off): return self._d('<Q', off)
    def bit(self, n): return (self._d('<B', n // 8) >> (n % 8)) & 1

    def ptr(self, i):
        if i >= self.psz: return None
        return self.m.struct_at(self.seg, self.w + self.dsz + i)

    def text(self, i):
        if i >= self.psz: return ''
        l = self.m.list_at(self.seg, self.w + self.dsz + i)
        if not l: return ''
        seg, start, esz, cnt = l
        return self.m.segs[seg][8 * start:8 * start + cnt - 1].decode(errors='replace')

    def slist(self, i):
        """Composite list of structs."""
        if i >= self.psz: return []
        l = self.m.list_at(self.seg, self.w + self.dsz + i)
        if not l: return []
        seg, start, esz, cnt = l
        if esz == 7:
            tag = self.m.word(seg, start)
            n = (tag >> 2) & 0x3FFFFFFF; dsz = (tag >> 32) & 0xFFFF; psz = tag >> 48
            return [S(self.m, seg, start + 1 + k * (dsz + psz), dsz, psz) for k in range(n)]
        if esz == 5:  # 8-byte elements (struct with 1 data word, e.g. Point)
            return [S(self.m, seg, start + k, 1, 0) for k in range(cnt)]
        if esz == 4:
            return [S(self.m, seg, start + k, 1, 0) for k in range(cnt)]  # approximate
        return []

    def u16list(self, i):
        l = self.m.list_at(self.seg, self.w + self.dsz + i)
        if not l: return []
        seg, start, esz, cnt = l
        return list(struct.unpack_from('<%dH' % cnt, self.m.segs[seg], 8 * start))


def pt(s):
    return (s.i32(0), s.i32(4)) if s else (None, None)


class Replay:
    pass


def load(path):
    raw = unpack(open(path, 'rb').read())
    m = Msg(raw)
    root = m.struct_at(0, 0)
    rp = Replay()
    rp.map_text = root.text(0); rp.bot_a = root.text(1); rp.bot_b = root.text(2)
    rp.format = root.u32(0)
    ev = []
    for e in root.slist(3):
        k = e.u16(0); b = e.ptr(0)
        if b is None: continue
        if k == 0: ev.append(('round', b.i32(0)))
        elif k == 1: ev.append(('turn', b.i32(0)))
        elif k == 2:
            x, y = pt(b.ptr(0)); ev.append(('countdown', x, y, b.i32(0)))
        elif k == 3:
            x, y = pt(b.ptr(0)); ev.append(('tile', x, y, b.bit(0)))
        elif k == 4:
            a = b.ptr(0); kind = arg = None
            if a:
                w = a.u16(0)
                if w == 0: kind, arg = 'move', ''.join(DIRS[d] for d in a.u16list(0))
                elif w == 1: kind, arg = 'split', a.i32(4)
                else: kind = 'suicide'
            ev.append(('action', b.i32(0), kind, arg))
        elif k == 5: ev.append(('elog', b.i32(0), b.text(0)))
        elif k == 6: ev.append(('log', b.i32(0), b.text(0)))
        elif k == 7: ev.append(('ind', b.i32(0), b.text(0)))
        elif k == 9:
            hx, hy = pt(b.ptr(0)); tx, ty = pt(b.ptr(1))
            ev.append(('update', b.i32(0), DIRS[b.u16(4) & 3], hx, hy, tx, ty))
        elif k == 10:
            ev.append(('split', b.i32(0), b.i32(4), 'AB'[b.u16(8) & 1], DIRS[b.u16(10) & 3],
                       [pt(q) for q in b.slist(0)], [pt(q) for q in b.slist(1)]))
        elif k == 11: ev.append(('death', b.i32(0), DEATH[min(b.u16(4), 4)]))
        elif k == 12:
            o = pt(b.ptr(0)); en = pt(b.ptr(1))
            ev.append(('sonar', b.i32(0), DIRS[b.u16(4) & 3], b.u64(16), o[0], o[1], en[0], en[1],
                       b.i32(12) if b.u16(6) == 1 else None, b.u16(24)))
    rp.events = ev
    res = root.ptr(4); rp.result = None
    if res:
        ta, tb = res.ptr(0), res.ptr(1)
        rp.result = dict(terminated=res.bit(0), end_reason=res.u16(2),
                         winner=('AB'[res.u16(6) & 1] if res.u16(4) == 1 else None),
                         A=(ta.i32(0), ta.i32(4), ta.i32(8)) if ta else None, B=(tb.i32(0), tb.i32(4), tb.i32(8)) if tb else None)
    return rp


if __name__ == '__main__':
    for p in sys.argv[1:]:
        rp = load(p)
        name = next((l for l in rp.map_text.split('\n') if l.startswith('MAP_NAME')), '?')
        from collections import Counter
        print(p.split('/')[-1], name, rp.bot_a, 'vs', rp.bot_b, 'fmt', rp.format, len(rp.events), 'events',
              dict(Counter(e[0] for e in rp.events)), rp.result)

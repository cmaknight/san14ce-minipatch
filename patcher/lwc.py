"""LWC 압축 블록 해제·압축. 형식은 docs/data-formats.md 참고.

블록 구조: "LWC\\x1a", u32 usize, u32 csize, u8 dict[256], u8 bits[csize] (MSB 우선).
"""
import struct
from collections import Counter

MAGIC = b'LWC\x1a'
HEADER_SIZE = 12 + 256


def find_block(data):
    off = data.find(MAGIC)
    if off < 0:
        raise ValueError('LWC block not found')
    return off


_MASK64 = (1 << 64) - 1


def _code_reader(bs):
    """MSB 우선 비트열 bs에서 code(v)를 하나씩 읽는 함수. 읽은 비트 수는 reader.pos."""
    pad = bytes(bs) + bytes(8)

    def read():
        pos = read.pos
        i = pos >> 3
        x = (int.from_bytes(pad[i:i + 8], 'big') << (pos & 7)) & _MASK64
        n = 65 - ((~x) & _MASK64).bit_length()  # 접두 = 1이 n-1개 + 0
        if 2 * n > 57:
            raise ValueError(f'code too long at bit {pos}')
        read.pos = pos + 2 * n
        return (1 << n) - 2 + ((x >> (64 - 2 * n)) & ((1 << n) - 1))

    read.pos = 0
    return read


def unlwc(data, off):
    """off 위치의 LWC 블록을 풀어 (해제 데이터, 사용 비트 수, 전체 비트 수)를 반환한다."""
    if data[off:off + 4] != MAGIC:
        raise ValueError('not an LWC block')
    usize, csize = struct.unpack_from('<II', data, off + 4)
    dic = data[off + 12:off + 12 + 256]
    bs = data[off + HEADER_SIZE:off + HEADER_SIZE + csize]
    if len(bs) != csize:
        raise ValueError('LWC block truncated')
    code = _code_reader(bs)
    out = bytearray()
    while len(out) < usize:
        c = code()
        if c < 256:
            out.append(dic[c])
        else:
            dist = c - 256
            length = code() + 3
            n = len(out)
            if dist <= 0 or dist > n:
                raise ValueError(f'bad distance {dist} at output {n}, bit {code.pos}')
            if dist >= length:
                out += out[n - dist:n - dist + length]
            else:
                for _ in range(length):
                    out.append(out[-dist])
    if code.pos > len(bs) * 8:
        raise ValueError('LWC block truncated')
    return bytes(out[:usize]), code.pos, len(bs) * 8


def tokens(data, off):
    """LWC 블록을 토큰 목록으로 푼다. 토큰 = (출력 위치, 거리, 길이), 리터럴은 거리 0·길이 1."""
    usize, csize = struct.unpack_from('<II', data, off + 4)
    code = _code_reader(data[off + HEADER_SIZE:off + HEADER_SIZE + csize])
    out = 0
    toks = []
    while out < usize:
        c = code()
        if c < 256:
            toks.append((out, 0, 1))
            out += 1
        else:
            length = code() + 3
            toks.append((out, c - 256, length))
            out += length
    return toks


def repatch(data, off, new):
    """원본 LWC 블록의 토큰을 최대한 유지한 채 해제 결과가 new가 되도록 고친 블록을 반환한다.

    사전과 토큰 구성이 원본과 같고, 값이 바뀐 바이트(와 그 바이트를 복사하던 역참조)만
    리터럴로 쪼갠다. new는 원본 해제 데이터와 길이가 같아야 한다.
    """
    usize = struct.unpack_from('<I', data, off + 4)[0]
    if len(new) != usize:
        raise ValueError('repatch는 길이가 같은 변경만 지원')
    dic = data[off + 12:off + HEADER_SIZE]
    inv = [0] * 256
    for i, b in enumerate(dic):
        inv[b] = i
    w = _BitWriter()

    def literal(p):
        w.code(inv[new[p]])

    for o, d, length in tokens(data, off):
        if d == 0:
            literal(o)
            continue
        ok = [new[o + k] == new[o + k - d] for k in range(length)]
        if all(ok):
            w.code(256 + d)
            w.code(length - 3)
            continue
        k = 0
        while k < length:
            if not ok[k]:
                literal(o + k)
                k += 1
                continue
            e = k
            while e < length and ok[e]:
                e += 1
            if e - k >= 3:
                w.code(256 + d)
                w.code(e - k - 3)
            else:
                for j in range(k, e):
                    literal(o + j)
            k = e
    bits = w.finish()
    return MAGIC + struct.pack('<II', usize, len(bits)) + bytes(dic) + bits


def block_end(data, off):
    """LWC 블록이 끝나는 위치(다음 바이트)."""
    csize = struct.unpack_from('<I', data, off + 8)[0]
    return off + HEADER_SIZE + csize


# --- 압축 ---
# code(v)는 n = floor(log2(v + 2))일 때 접두 n비트(1이 n-1개, 0 하나) + 값 n비트, 합계 2n비트다.

def _code_bits(v):
    return 2 * ((v + 2).bit_length() - 1)


class _BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def code(self, v):
        n = (v + 2).bit_length() - 1
        base = (1 << n) - 2
        self.acc = (self.acc << (2 * n)) | (base << n) | (v - base)
        self.n += 2 * n
        while self.n >= 8:
            self.n -= 8
            self.out.append((self.acc >> self.n) & 0xFF)
        self.acc &= (1 << self.n) - 1

    def finish(self):
        if self.n:
            self.out.append((self.acc << (8 - self.n)) & 0xFF)
            self.acc = 0
            self.n = 0
        return bytes(self.out)


# 원본 압축기 출력의 한도(docs/data-formats.md). 원본과 같은 범위로 맞춘다.
# 이 한도만으로는 게임의 DATA ERROR가 사라지지 않는다(fixdata는 헤더 서명도 맞춰야 한다).
MAX_DIST = 8191
MAX_LEN = 512


def lwc(data, window=MAX_DIST, chain=48, max_len=MAX_LEN):
    """data를 LWC 블록(매직 포함)으로 압축한다.

    사전은 바이트 빈도순, LZ 일치는 해시 체인 + 1단계 지연 평가로 비트 비용이 가장 작은 쪽을 고른다.
    원본 압축기와 같은 바이트열을 만들지는 않는다. 해제 결과만 같다.
    """
    if window > MAX_DIST or max_len > MAX_LEN:
        raise ValueError('window/max_len이 게임 해제기 한도를 넘음')
    data = bytes(data)
    n = len(data)
    freq = Counter(data)
    order = sorted(range(256), key=lambda b: (-freq[b], b))
    inv = [0] * 256
    for i, b in enumerate(order):
        inv[b] = i
    lit_bits = [_code_bits(inv[b]) for b in range(256)]
    pre = [0] * (n + 1)
    for i, b in enumerate(data):
        pre[i + 1] = pre[i] + lit_bits[b]

    heads = {}

    def insert(i):
        if i + 3 <= n:
            heads.setdefault(data[i:i + 3], []).append(i)

    def match_len(j, i, limit):
        length = 3
        step = 32
        while length + step <= limit and data[j + length:j + length + step] == data[i + length:i + length + step]:
            length += step
        while length < limit and data[j + length] == data[i + length]:
            length += 1
        return length

    def best(i):
        """(이득 비트, 길이, 거리). 이득 = 리터럴로 썼을 때 비트 - 일치로 썼을 때 비트."""
        if i + 3 > n:
            return 0, 0, 0
        cands = heads.get(data[i:i + 3])
        if not cands:
            return 0, 0, 0
        limit = min(max_len, n - i)
        bg = bl = bd = 0
        for j in reversed(cands[-chain:]):
            d = i - j
            if d > window:
                break
            length = match_len(j, i, limit)
            gain = pre[i + length] - pre[i] - _code_bits(256 + d) - _code_bits(length - 3)
            if gain > bg:
                bg, bl, bd = gain, length, d
            if length == limit:
                break
        return bg, bl, bd

    w = _BitWriter()
    i = 0
    nxt = None
    while i < n:
        cur = nxt if nxt is not None else best(i)
        nxt = None
        if cur[0] > 0 and i + 1 < n:
            insert(i)
            ahead = best(i + 1)
            if ahead[0] > cur[0] + lit_bits[data[i]]:
                w.code(inv[data[i]])
                i += 1
                nxt = ahead
                continue
            gain, length, dist = cur
            w.code(256 + dist)
            w.code(length - 3)
            for k in range(i + 1, i + length):
                insert(k)
            i += length
        elif cur[0] > 0:
            gain, length, dist = cur
            insert(i)
            w.code(256 + dist)
            w.code(length - 3)
            i += length
        else:
            insert(i)
            w.code(inv[data[i]])
            i += 1
    bits = w.finish()
    return MAGIC + struct.pack('<II', n, len(bits)) + bytes(order) + bits

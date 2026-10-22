import os
import random
import unittest

from patcher.lwc import MAX_DIST, MAX_LEN, lwc, unlwc

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'game-copy')


def tokens(block):
    """블록의 역참조 (거리, 길이) 목록."""
    bs = block[268:]
    usize = int.from_bytes(block[4:8], 'little')
    pos = 0

    def code():
        nonlocal pos
        c = n = 0
        while True:
            b = (bs[pos >> 3] >> (7 - (pos & 7))) & 1
            pos += 1
            c = (c << 1) | b
            n += 1
            if not b:
                break
        c2 = 0
        for _ in range(n):
            c2 = (c2 << 1) | ((bs[pos >> 3] >> (7 - (pos & 7))) & 1)
            pos += 1
        return c + c2

    out = 0
    refs = []
    while out < usize:
        c = code()
        if c < 256:
            out += 1
        else:
            length = code() + 3
            refs.append((c - 256, length))
            out += length
    return refs


class LwcTest(unittest.TestCase):
    def roundtrip(self, data):
        block = lwc(data)
        out, used, total = unlwc(block, 0)
        self.assertEqual(out, data)
        self.assertLess(total - used, 8)
        for dist, length in tokens(block):
            self.assertLessEqual(dist, MAX_DIST)
            self.assertLessEqual(length, MAX_LEN)

    def test_small(self):
        for data in [b'', b'a', b'ab', b'aaa', b'abcabcabcabc', bytes(range(256)) * 3,
                     bytes(1) * 5000, bytes(range(256)) * 40]:
            self.roundtrip(data)

    def test_random(self):
        rnd = random.Random(14)
        for _ in range(20):
            alphabet = bytes(rnd.sample(range(256), rnd.randint(1, 256)))
            self.roundtrip(bytes(rnd.choice(alphabet) for _ in range(rnd.randint(0, 5000))))

    @unittest.skipUnless(os.path.isdir(GAME), 'game-copy 없음')
    def test_game_files(self):
        from patcher.lwc import find_block
        for lang in ['JP', 'KO', 'SC', 'TC']:
            p = os.path.join(GAME, f'0010_{lang}', 'fixdataexce.s14')
            with open(p, 'rb') as f:
                data = f.read()
            payload = unlwc(data, find_block(data))[0]
            self.roundtrip(payload)


@unittest.skipUnless(os.path.isdir(GAME), 'game-copy 없음')
class RepatchTest(unittest.TestCase):
    def test_identity_and_edit(self):
        from patcher.lwc import find_block, repatch
        for lang in ['JP', 'KO', 'SC', 'TC']:
            with open(os.path.join(GAME, f'0010_{lang}', 'fixdataexce.s14'), 'rb') as f:
                data = f.read()
            off = find_block(data)
            payload = unlwc(data, off)[0]
            self.assertEqual(repatch(data, off, payload), data[off:])
            edited = bytearray(payload)
            for p in (0, 0x1000, len(payload) - 1):
                edited[p] ^= 0x5A
            self.assertEqual(unlwc(repatch(data, off, bytes(edited)), 0)[0], bytes(edited))


if __name__ == '__main__':
    unittest.main()

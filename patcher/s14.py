""".s14 파일 = 평문 헤더 + LWC 블록. fixdata는 헤더에 본문 서명이 있고, DLC 시나리오는 전체가 XOR로 감싸여 있다(docs/data-formats.md 참고)."""
import struct

from . import lwc

# fixdata 서명: 헤더 SIG_OFF의 u32 = (해제 데이터 바이트 합 & 0xFFFF) ^ SIG_EXP mod SIG_MOD.
# 게임은 v ^ SIG_CHECK_EXP mod SIG_MOD 가 바이트 합과 같은지 검사하고, 다르면 DATA ERROR.
SIG_OFF = 463
SIG_MOD = 1071514531
SIG_EXP = 5
SIG_CHECK_EXP = 35714969


def checksum(payload):
    return sum(payload) & 0xFFFF


def signature(payload):
    return pow(checksum(payload), SIG_EXP, SIG_MOD)


def signature_ok(header, payload):
    if len(header) < SIG_OFF + 4:
        return False
    v = struct.unpack_from('<I', header, SIG_OFF)[0]
    return pow(v, SIG_CHECK_EXP, SIG_MOD) == checksum(payload)


MAGIC = b'SN14'

# DLC 시나리오(DLC/0010_*/scedaexceNN.s14)는 파일 전체를 XOR 키 스트림으로 감쌌다. seed = DLC_ID + 시나리오 번호.
# DLC_ID는 exe의 DLC 표에서 구한다(docs/data-formats.md). 같은 함수로 풀고 싼다.
# CE exe(1.0.1)의 DLC 표: 시나리오 번호 → DLC_ID. 10·32·33만 우리 사본에 있고 11·19·29는 파일을 본 적이 없다.
DLC_SCENARIOS = {10: 1198830, 11: 1198831, 19: 1258154, 29: 1431480, 32: 4598950, 33: 4598960}
DLC_IDS = tuple(sorted(set(DLC_SCENARIOS.values()) | {1198832}))  # 1198832 = 구판 sceda15


def dlc_xor(data, seed):
    out = bytearray(data)
    s = seed
    for i in range(len(out)):
        s = (s * 1812433253 + 12345) & 0xFFFFFFFF
        out[i] ^= ((s >> 16) ^ (s >> 24)) & 0xFF
    return bytes(out)


def find_dlc_seed(data):
    """평문이 아닌 파일에서 앞 8바이트가 SN14 헤더로 풀리는 seed. 없으면 None."""
    for dlc in DLC_IDS:
        for n in range(1, 256):
            if dlc_xor(data[:8], dlc + n)[4:8] == MAGIC:
                return dlc + n
    return None


class S14:
    def __init__(self, data):
        self.original = bytes(data)
        self.seed = None
        if data[4:8] != MAGIC:
            self.seed = find_dlc_seed(data)
            if self.seed is None:
                raise ValueError('S14 헤더가 아니고 알려진 DLC 키로도 풀리지 않음')
            data = dlc_xor(data, self.seed)
        self._plain = bytes(data)
        self.block = lwc.find_block(data)
        payload, _, _ = lwc.unlwc(data, self.block)
        self.header = data[:self.block]
        self.trailer = data[lwc.block_end(data, self.block):]
        self.payload = bytearray(payload)
        self._pristine = bytes(payload)
        self.signed = signature_ok(self.header, payload)

    def to_bytes(self):
        if self.payload == self._pristine:
            return self.original
        # 원본 토큰을 유지하는 최소 수정을 쓴다. 길이가 바뀌면 전체 재압축.
        if len(self.payload) == len(self._pristine):
            block = lwc.repatch(self._plain, self.block, self.payload)
        else:
            block = lwc.lwc(self.payload)
        check, _, _ = lwc.unlwc(block, 0)
        if check != self.payload:
            raise RuntimeError('LWC 재압축 검증 실패')
        header = bytearray(self.header)
        if self.signed:
            struct.pack_into('<I', header, SIG_OFF, signature(self.payload))
            assert signature_ok(header, self.payload)
        out = bytes(header) + block + self.trailer
        return dlc_xor(out, self.seed) if self.seed is not None else out


CODECS = {'.s14': S14}

"""그룹 2: 개성 패치. 데이터 부분(효과2, 설명문). 근거는 docs/02-traits.md.

개성 정의는 두 곳을 고친다.
- fixdata(`0010_*/fixdataexce.s14`): 설명문. 효과 칸도 같은 값으로 맞춘다.
- 시나리오 사본(`0010_*`, `DLC/0010_*`의 `scedaexce*.s14`): 새 게임은 여기서 조건·효과를 읽는다.
진행 중인 세이브에는 세이브 자체의 사본이 있어 반영되지 않는다.
구심·악주의 저돌 효과 무효는 DLL 기능(dll_flag)이다.
"""
import glob
import os
import re
import struct

from ..s14 import DLC_SCENARIOS, S14
from .base import Group, PatchError

CHUNK = 22
COUNT = 356
NEXT_CHUNK = 23
SCEN_STRIDE = 60
# fixdata 레코드 크기는 언어마다 다르다(KO·SC·TC 170, JP 150: 설명문 51자/41자). 뒤쪽 필드는 레코드 끝 기준으로 같다.
FIX_STRIDES = (170, 150)
DESC_OFF = 44

JEODOL, GUSIM, AKJU, TUJANG = 1, 21, 23, 108
GONGGUN = 5  # 효과 ID 5 = 공군. 효과치 1단계 = 5%(소탕 6 = 1.3배, 효장 3 = 1.15배)

# (개성 ID, 원래 (조건1, 조건2, 효과1, 효과치1), 효과2 원래 → 새 (효과, 효과치))
EFFECTS = [
    (JEODOL, (0, 0, 1, 1), (0, 0), (GONGGUN, 1)),   # 저돌: 공군 1단계(5%)
    (TUJANG, (26, 1, 55, 1), (0, 0), (GONGGUN, 2)),  # 투장: 공군 2단계(10%). 조건 26(포위)을 공유해 포위 시에만
]

# 설명문: 언어 → {개성 ID: (원문, 새 문구)}. 원문이 다르면 적용을 거부한다.
DESCS = {
    'KO': {
        JEODOL: ('명령설정에서 적접근시「무시」，자주퇴각「허가」선택불가',
                 '자신부대 공군상승，명령설정에서 적접근시「무시」，자주퇴각「허가」선택불가'),
        TUJANG: ('자신부대가 포위효과의 영향을 받지 않음',
                 '자신부대가 포위효과의 영향을 받지 않음，포위 시 공군상승'),
        GUSIM: ('자신부대를 제외한 특정범위 내 자신세력부대의 사기저하 적음',
                '자신부대를 제외한 특정범위 내 자신세력부대의 사기저하 적음，저돌효과 무효'),
        AKJU: ('다음 턴개시 시，자신부대를 제외한 특정범위 내 자신세력부대의 사기상승',
               '다음 턴개시 시，자신부대를 제외한 특정범위 내 자신세력부대의 사기상승，저돌효과 무효'),
    },
    'JP': {
        JEODOL: ('命令設定で、敵接近時「無視」、自主退却「許可」が選択不可',
                 '自部隊の攻軍が上昇、命令設定で敵接近時「無視」、自主退却「許可」が選択不可'),
        TUJANG: ('自部隊が包囲効果の影響を受けない',
                 '自部隊が包囲効果の影響を受けず、包囲時に攻軍が上昇'),
        GUSIM: ('自部隊を除く特定範囲内の自勢力部隊の士気を低下しにくくする',
                '自部隊を除く特定範囲内の自勢力部隊の士気を低下しにくくし、猪突効果を無効化'),
        AKJU: ('次ターン開始時、自部隊を除く特定範囲内の自勢力部隊の士気が上昇',
               '次ターン開始時、自部隊を除く特定範囲内の自勢力部隊の士気が上昇、猪突効果無効'),
    },
    'SC': {
        JEODOL: ('在命令设定中无法选择：敌接近时【无视】，自主撤退【许可】',
                 '自部队的攻军上升，在命令设定中无法选择：敌接近时【无视】，自主撤退【许可】'),
        TUJANG: ('自部队不受包围效果的影响',
                 '自部队不受包围效果的影响，被包围时攻军上升'),
        GUSIM: ('除自部队之外，特定范围内的自势力部队不易降低士气',
                '除自部队之外，特定范围内的自势力部队不易降低士气，莽撞效果无效'),
        AKJU: ('下一回合开始时，除自部队之外的特定范围内的自势力部队士气上升',
               '下一回合开始时，除自部队之外的特定范围内的自势力部队士气上升，莽撞效果无效'),
    },
    'TC': {
        JEODOL: ('在命令設定中無法選擇：敵接近時「無視」、主動撤退「許可」。',
                 '提升自部隊的攻軍，在命令設定中無法選擇：敵接近時「無視」、主動撤退「許可」。'),
        TUJANG: ('自部隊不受包圍效果的影響。',
                 '自部隊不受包圍效果的影響，被包圍時提升攻軍。'),
        GUSIM: ('自部隊之外的特定範圍內的自勢力部隊之士氣不易下降。',
                '自部隊之外的特定範圍內的自勢力部隊之士氣不易下降，莽撞效果無效。'),
        AKJU: ('下回合開始時，除自部隊之外的特定範圍內自勢力部隊的士氣上升。',
               '下回合開始時，除自部隊之外的特定範圍內自勢力部隊的士氣上升，莽撞效果無效。'),
    },
}


class Layout:
    """개성 레코드 필드 위치. fixdata는 레코드 끝 기준, 시나리오 사본은 고정 위치."""

    def __init__(self, stride):
        self.stride = stride
        if stride == SCEN_STRIDE:
            self.cond, self.eff1, self.eff2, self.desc = 44, 54, 57, None
        else:
            self.cond, self.eff1, self.eff2 = stride - 22, stride - 18, stride - 15
            self.desc = (DESC_OFF, stride - 24)


def find_table(buf, strides, what):
    """청크 (22, 356) 헤더 뒤 개성 표의 시작과 레코드 크기. 표 끝 다음 u32가 청크 23이어야 한다."""
    head = struct.pack('<II', CHUNK, COUNT)
    found = []
    p = buf.find(head)
    while p >= 0:
        base = p + 8
        for s in strides:
            end = base + COUNT * s
            if end + 4 <= len(buf) and struct.unpack_from('<I', buf, end)[0] == NEXT_CHUNK:
                found.append((base, s))
        p = buf.find(head, p + 1)
    if len(found) != 1:
        raise PatchError(f'{what}: 개성 표를 하나로 특정하지 못함({len(found)}곳)')
    return found[0]


def _effect(buf, pos):
    return buf[pos], struct.unpack_from('<h', buf, pos + 1)[0]


def _lang(rel):
    d = rel.split('/')[-2]
    return d.split('_', 1)[1] if '_' in d else ''


class Traits(Group):
    id = 2
    key = 'traits'
    name = '개성 조정'
    desc = '구심·악주 효과 발동 시 저돌의 돌진 효과가 무효, 저돌 공군 5%, 투장 포위 시 공군 10% 증가'
    new_game = True
    implemented = True
    dll_flag = 'trait_balance'

    def targets(self, game):
        pats = [os.path.join(game, '0010_*', 'fixdataexce.s14'),
                os.path.join(game, '0010_*', 'scedaexce*.s14'),
                os.path.join(game, 'DLC', '0010_*', 'scedaexce*.s14')]
        files = [f for p in pats for f in glob.glob(p)]
        rels = (os.path.relpath(f, game).replace(os.sep, '/') for f in files)
        return sorted(r for r in rels if _lang(r) in DESCS)

    def verify_unknown(self, rel, data):
        """원본 해시를 모르는 시나리오를 내용으로 확인한다. 우리 사본에 없는 DLC(11·19·29)나
        다른 도구로 저장한 시나리오가 여기 온다. 풀리고, 개성 표의 바꿀 칸이 원래 값이면 원본으로 본다.
        DLC는 exe DLC 표의 번호이고 그 번호의 키로 풀려야 한다.
        반환: 판정 대상이 아니면 None, 원본이면 True, 아니면 이유 문자열(진단 로그용)."""
        m = re.fullmatch(r'(DLC/)?0010_[A-Z]+/scedaexce(\d+)\.s14', rel)
        if not m or _lang(rel) not in DESCS:
            return None
        dlc, n = bool(m[1]), int(m[2])
        if dlc and n not in DLC_SCENARIOS:
            return f'exe DLC 표에 없는 시나리오 번호 {n}(알려진 번호 {sorted(DLC_SCENARIOS)})'
        try:
            doc = S14(data)
        except Exception as e:
            return f'파일을 풀지 못함(헤더 {bytes(data[:8]).hex()}): {e}'
        try:
            base, stride = find_table(doc.payload, (SCEN_STRIDE,), rel)
        except PatchError as e:
            return f'개성 표를 찾지 못함(해제 {len(doc.payload)} B, seed {doc.seed}): {e}'
        if dlc and doc.seed != DLC_SCENARIOS[n] + n:
            return f'DLC 키가 다름(seed {doc.seed}, 예상 {DLC_SCENARIOS[n] + n})'
        lay = Layout(stride)
        buf = doc.payload
        for tid, (c1, c2, e1, v1), old2, _ in EFFECTS:
            r = base + tid * stride
            got = (buf[r + lay.cond], buf[r + lay.cond + 1]) + _effect(buf, r + lay.eff1)
            eff2 = _effect(buf, r + lay.eff2)
            if got != (c1, c2, e1, v1) or eff2 != old2:
                return (f'개성 {tid} 값이 원본과 다름(조건·효과1 {got}, 예상 {(c1, c2, e1, v1)}; '
                        f'효과2 {eff2}, 예상 {old2})')
        return True

    def patch(self, rel, doc):
        fix = rel.endswith('fixdataexce.s14')
        if fix and not doc.signed:
            raise PatchError(f'{rel}: 헤더 서명을 확인할 수 없어 수정하면 DATA ERROR가 난다')
        buf = doc.payload
        base, stride = find_table(buf, FIX_STRIDES if fix else (SCEN_STRIDE,), rel)
        lay = Layout(stride)
        changes = []
        for tid, (c1, c2, e1, v1), old2, new2 in EFFECTS:
            r = base + tid * stride
            got = (buf[r + lay.cond], buf[r + lay.cond + 1]) + _effect(buf, r + lay.eff1)
            if got != (c1, c2, e1, v1) or _effect(buf, r + lay.eff2) != old2:
                raise PatchError(f'{rel} 개성 {tid}: 예상과 다른 값 {got} 효과2 {_effect(buf, r + lay.eff2)}')
            struct.pack_into('<Bh', buf, r + lay.eff2, *new2)
            changes.append(f'개성 {tid} 효과2 {old2}→{new2}')
        if fix:
            start, end = lay.desc
            size = end - start
            for tid, (old, new) in DESCS[_lang(rel)].items():
                r = base + tid * stride
                cur = bytes(buf[r + start:r + end]).decode('utf-16le').split('\0')[0]
                if cur != old:
                    raise PatchError(f'{rel} 개성 {tid}: 설명문이 예상과 다름: {cur}')
                enc = new.encode('utf-16le')
                if len(enc) > size:
                    raise PatchError(f'{rel} 개성 {tid}: 설명문이 {size // 2}자를 넘음({len(new)}자)')
                buf[r + start:r + end] = enc.ljust(size, b'\0')
                changes.append(f'개성 {tid} 설명문 {len(old)}→{len(new)}자')
        return changes

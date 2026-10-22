"""그룹 5: 진형 밸런스 패치. 근거는 docs/05-formation.md."""
import glob
import os

from .base import Group, PatchError, find_unique

# 구분(1=육상), 기동, 점령3, 점령5, 미상, 공군, 공성, 파성, 방어
BONGSI = bytes([0x01, 20, 110, 180, 0, 45, 13, 8, 18])
HAKIK = bytes([0x01, 16, 30, 60, 0, 18, 12, 13, 30])
ATK = 5
DEF = 8

# (설명, 레코드 패턴, 패턴 기준 필드 위치, 원래 값, 새 값)
# 봉시 공군 45→48은 뺐다. 게임이 진형 공군 45 초과분을 잘라 반영되지 않는다(docs/05-formation.md).
EDITS = [
    ('봉시 방어', BONGSI, DEF, 18, 20),
    ('학익 방어', HAKIK, DEF, 30, 36),
]


class Formation(Group):
    id = 5
    key = 'formation'
    name = '진형 밸런스 조정'
    desc = '학익 방어 30 → 36, 봉시 방어 18 → 20'
    implemented = True

    def targets(self, game):
        files = glob.glob(os.path.join(game, '0010_*', 'fixdataexce.s14'))
        return sorted(os.path.relpath(f, game).replace(os.sep, '/') for f in files)

    def patch(self, rel, doc):
        if not doc.signed:
            raise PatchError(f'{rel}: 헤더 서명을 확인할 수 없어 수정하면 DATA ERROR가 난다')
        buf = doc.payload
        found = {}
        for what, pat, _, _, _ in EDITS:
            key = pat
            if key not in found:
                found[key] = find_unique(buf, pat, f'{rel} {what}')
        changes = []
        for what, pat, field, old, new in EDITS:
            pos = found[pat] + field
            if buf[pos] != old:
                raise PatchError(f'{rel} {what}: 예상 값 {old}, 실제 {buf[pos]}')
            buf[pos] = new
            changes.append(f'{what} {old}→{new} @{pos:#x}')
        return changes

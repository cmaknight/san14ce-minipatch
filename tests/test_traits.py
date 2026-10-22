"""그룹 2(개성 데이터) 테스트. game-copy의 일부 파일을 임시 폴더로 복사해 쓴다(원본은 건드리지 않음)."""
import os
import shutil
import struct
import tempfile
import unittest

from patcher import core
from patcher.groups import PatchError
from patcher.groups.traits import DESCS, Layout, find_table
from patcher.s14 import S14, signature_ok

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'game-copy')
FIX = {lang: f'0010_{lang}/fixdataexce.s14' for lang in ['JP', 'KO', 'SC', 'TC']}
SCEN = ['0010_KO/scedaexce01.s14', '0010_JP/scedaexce01.s14', 'DLC/0010_KO/scedaexce32.s14']


def payload(data):
    return S14(data).payload


@unittest.skipUnless(os.path.isdir(GAME), 'game-copy 없음')
class TraitsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = tempfile.mkdtemp(prefix='san14src-')
        shutil.copy2(os.path.join(GAME, 'SAN14CE_KO.exe'), cls.src)
        for rel in list(FIX.values()) + SCEN:
            dst = os.path.join(cls.src, *rel.split('/'))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(GAME, *rel.split('/')), dst)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.src)

    def setUp(self):
        self.game = tempfile.mkdtemp(prefix='san14game-')
        shutil.copytree(self.src, self.game, dirs_exist_ok=True)
        self.eng = core.Engine(self.game, data_only=True)
        self.orig = {rel: self.eng.read(rel) for rel in self.eng.targets()}

    def tearDown(self):
        shutil.rmtree(self.game)

    def apply(self, ids):
        self.eng.apply(ids, log=lambda *a: None)

    def test_targets(self):
        self.assertEqual(sorted(self.orig), sorted(list(FIX.values()) + SCEN))

    def test_requires_dll_without_data_only(self):
        from unittest import mock
        from patcher import dll
        eng = core.Engine(self.game)
        with mock.patch.object(dll, 'BUILD', os.path.join(self.game, 'no-such.dll')):
            with self.assertRaises(PatchError):
                eng.apply([2], log=lambda *a: None)
        for rel, data in self.orig.items():
            self.assertEqual(eng.read(rel), data)
        self.assertFalse(os.path.exists(os.path.join(self.game, core.STATE_DIR)))

    def test_dll_install_and_remove(self):
        from unittest import mock
        from patcher import dll
        fake = os.path.join(self.game, 'fake-build.dll')
        with open(fake, 'wb') as f:
            f.write(b'MZ fake dll')
        target = os.path.join(self.game, dll.DLL_NAME)
        ini = os.path.join(self.game, dll.CONFIG_NAME)
        with mock.patch.object(dll, 'BUILD', fake):
            eng = core.Engine(self.game)
            eng.apply([2, 5], log=lambda *a: None)
            self.assertTrue(os.path.exists(target))
            with open(ini, encoding='utf-8') as f:
                text = f.read()
            self.assertIn('trait_balance=1', text)
            self.assertIn('auto_search=0', text)
            self.assertTrue(eng.status(log=lambda *a: None))
            eng.apply([5], log=lambda *a: None)
            self.assertFalse(os.path.exists(target))
            self.assertFalse(os.path.exists(ini))

    def test_foreign_dll_refused_before_writing(self):
        from unittest import mock
        from patcher import dll
        fake = os.path.join(self.game, 'fake-build.dll')
        with open(fake, 'wb') as f:
            f.write(b'MZ fake dll')
        with open(os.path.join(self.game, dll.DLL_NAME), 'wb') as f:
            f.write(b'MZ someone else')
        with mock.patch.object(dll, 'BUILD', fake):
            with self.assertRaises(PatchError):
                core.Engine(self.game).apply([2], log=lambda *a: None)
        for rel, data in self.orig.items():
            self.assertEqual(self.eng.read(rel), data)

    def test_scenario_changes_only_effect2(self):
        self.apply([2])
        for rel in SCEN:
            before, after = payload(self.orig[rel]), payload(self.eng.read(rel))
            base, stride = find_table(before, (60,), rel)
            diff = {i - base: (before[i], after[i]) for i in range(len(before)) if before[i] != after[i]}
            self.assertEqual(diff, {1 * 60 + 57: (0, 5), 1 * 60 + 58: (0, 1),
                                    108 * 60 + 57: (0, 5), 108 * 60 + 58: (0, 2)}, rel)

    def test_dlc_file_stays_wrapped(self):
        rel = 'DLC/0010_KO/scedaexce32.s14'
        self.apply([2])
        data = self.eng.read(rel)
        self.assertNotEqual(data[4:8], b'SN14')
        self.assertEqual(S14(data).seed, S14(self.orig[rel]).seed)

    def test_fixdata_effects_and_descriptions(self):
        self.apply([2, 5])
        for lang, rel in FIX.items():
            doc = S14(self.eng.read(rel))
            self.assertTrue(signature_ok(doc.header, doc.payload), rel)
            base, stride = find_table(doc.payload, (170, 150), rel)
            lay = Layout(stride)
            for tid, eff in [(1, (5, 1)), (108, (5, 2))]:
                r = base + tid * stride
                self.assertEqual(struct.unpack_from('<Bh', doc.payload, r + lay.eff2), eff, rel)
            for tid, (_, new) in DESCS[lang].items():
                r = base + tid * stride
                text = bytes(doc.payload[r + lay.desc[0]:r + lay.desc[1]]).decode('utf-16le').split('\0')[0]
                self.assertEqual(text, new, rel)
        # 그룹 1 변경도 같은 파일에 함께 들어간다
        self.assertEqual(payload(self.eng.read(FIX['KO']))[0x2D787], 36)

    def test_restore(self):
        self.apply([2, 5])
        self.apply([])
        for rel, data in self.orig.items():
            self.assertEqual(self.eng.read(rel), data, rel)

    def test_switch_groups(self):
        self.apply([2])
        self.apply([5])
        for rel in SCEN:
            self.assertEqual(self.eng.read(rel), self.orig[rel], rel)
        self.assertEqual(payload(self.eng.read(FIX['KO']))[0x2D787], 36)

    # 우리 사본에 없는 DLC(11·19·29)는 원본 해시를 모른다. 2026-10-21 제보: DLC/0010_JP/scedaexce11.s14에서 적용 거부.
    def make_unseen_dlc(self, n=11, edit=None):
        """DLC 32 파일을 DLC n 키로 다시 싸서 처음 보는 DLC 시나리오처럼 만든다."""
        from patcher.s14 import DLC_SCENARIOS, dlc_xor
        doc = S14(self.orig['DLC/0010_KO/scedaexce32.s14'])
        if edit:
            edit(doc.payload)
        plain = dlc_xor(doc.to_bytes(), doc.seed)
        rel = f'DLC/0010_KO/scedaexce{n}.s14'
        data = dlc_xor(plain, DLC_SCENARIOS[n] + n)
        with open(os.path.join(self.game, *rel.split('/')), 'wb') as f:
            f.write(data)
        return rel, data

    def test_unseen_dlc_verified_by_content(self):
        rel, data = self.make_unseen_dlc()
        self.assertEqual(self.eng.apply([2], log=lambda *a: None), [])
        after = self.eng.read(rel)
        self.assertNotEqual(after, data)
        self.assertEqual(S14(after).seed, S14(data).seed)
        self.assertTrue(self.eng.status(log=lambda *a: None))
        self.apply([])
        self.assertEqual(self.eng.read(rel), data)

    def test_modified_unseen_scenario_is_skipped(self):
        def edit(buf):  # 다른 도구가 저돌 효과2를 이미 바꾼 시나리오
            base, stride = find_table(buf, (60,), 'scen')
            struct.pack_into('<Bh', buf, base + 1 * stride + Layout(stride).eff2, 5, 3)
        rel, data = self.make_unseen_dlc(edit=edit)
        logs = []
        skipped = self.eng.apply([2], log=logs.append)
        self.assertEqual(skipped, [rel])
        # 진단 로그에 어느 값이 달랐는지 남는다
        self.assertTrue(any('개성 1 값이 원본과 다름' in m for m in logs), logs)
        self.assertEqual(self.eng.read(rel), data)
        self.assertNotEqual(self.eng.read(SCEN[0]), self.orig[SCEN[0]])

    def test_unknown_fixdata_refused_before_writing(self):
        p = os.path.join(self.game, *FIX['KO'].split('/'))
        with open(p, 'ab') as f:
            f.write(b'\0')
        with self.assertRaises(core.UnknownFile) as cm:
            self.apply([2])
        self.assertIn('sha256', cm.exception.detail)
        for rel in SCEN:
            self.assertEqual(self.eng.read(rel), self.orig[rel], rel)
        self.assertFalse(os.path.exists(os.path.join(self.game, core.STATE_DIR)))


    def test_diag_collect(self):
        from patcher import diag
        self.make_unseen_dlc()
        lines = diag.collect(self.game)
        text = '\n'.join(lines)
        self.assertIn('SAN14CE_KO.exe', text)
        self.assertIn('external: scedaexce11.s14', text)
        self.assertFalse(any('수집 실패' in m for m in lines), text)
        self.assertIn('존재 False', '\n'.join(diag.collect(os.path.join(self.game, 'nope'))))


if __name__ == '__main__':
    unittest.main()

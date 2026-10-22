"""패처 엔진 테스트. game-copy의 실행 파일과 0010_* 폴더를 임시 폴더로 복사해 쓴다(원본은 건드리지 않음)."""
import json
import os
import shutil
import tempfile
import unittest

from patcher import core, dll
from patcher.groups import PatchError
from patcher.lwc import find_block, unlwc

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'game-copy')
KO = '0010_KO/fixdataexce.s14'
# 해제 스트림 오프셋(docs/05-formation.md): 봉시 방어, 학익 방어
KO_OFFSETS = {0x2D5BF: (18, 20), 0x2D787: (30, 36)}


@unittest.skipUnless(os.path.isdir(GAME), 'game-copy 없음')
class EngineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = tempfile.mkdtemp(prefix='san14src-')
        for exe in ['SAN14CE_KO.exe', 'SAN14CE.exe']:
            shutil.copy2(os.path.join(GAME, exe), cls.src)
        for lang in ['JP', 'KO', 'SC', 'TC']:
            os.makedirs(os.path.join(cls.src, f'0010_{lang}'))
            shutil.copy2(os.path.join(GAME, f'0010_{lang}', 'fixdataexce.s14'), os.path.join(cls.src, f'0010_{lang}'))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.src)

    def setUp(self):
        self.game = tempfile.mkdtemp(prefix='san14game-')
        shutil.copytree(self.src, self.game, dirs_exist_ok=True)
        self.eng = core.Engine(self.game, allow_blocked=True)
        self.orig = {rel: self.eng.read(rel) for rel in self.eng.targets()}

    def tearDown(self):
        shutil.rmtree(self.game)

    def apply(self, ids):
        self.eng.apply(ids, log=lambda *a: None)

    def payload(self, rel):
        data = self.eng.read(rel)
        return unlwc(data, find_block(data))[0]

    def test_targets(self):
        self.assertEqual(self.eng.targets(), [f'0010_{x}/fixdataexce.s14' for x in ['JP', 'KO', 'SC', 'TC']])

    def test_apply_formation_changes_only_target_bytes(self):
        before = self.payload(KO)
        self.apply([5])
        after = self.payload(KO)
        diff = {i: (before[i], after[i]) for i in range(len(before)) if before[i] != after[i]}
        self.assertEqual(len(before), len(after))
        self.assertEqual(diff, KO_OFFSETS)
        for rel in self.orig:
            self.assertEqual(self.eng.inspect(rel, self.eng.load_state()), 'patched')
        self.assertTrue(os.path.exists(self.eng.backup_path(KO)))

    def test_signature_updated(self):
        from patcher.s14 import signature_ok
        self.apply([5])
        for rel in self.orig:
            data = self.eng.read(rel)
            off = find_block(data)
            self.assertTrue(signature_ok(data[:off], unlwc(data, off)[0]), rel)

    def test_restore(self):
        self.apply([5])
        self.apply([])
        for rel, data in self.orig.items():
            self.assertEqual(self.eng.read(rel), data)

    def test_reapply_is_stable(self):
        self.apply([5])
        first = self.eng.read(KO)
        self.apply([5])
        self.assertEqual(self.eng.read(KO), first)

    def test_steam_verify_restores_original(self):
        self.apply([5])
        shutil.copy2(os.path.join(GAME, *KO.split('/')), self.eng.path(KO))
        self.assertFalse(self.eng.status(log=lambda *a: None))
        self.apply([5])
        self.assertEqual(self.payload(KO)[0x2D787], 36)

    def test_unknown_file_refused(self):
        with open(self.eng.path(KO), 'ab') as f:
            f.write(b'\0')
        with self.assertRaises(PatchError):
            self.apply([5])
        self.assertFalse(os.path.exists(self.eng.backup_path(KO)))

    def test_restore_over_unknown_file_uses_backup(self):
        self.apply([5])
        with open(self.eng.path(KO), 'ab') as f:
            f.write(b'x')
        self.apply([])
        self.assertEqual(self.eng.read(KO), self.orig[KO])

    def test_minimal_edit_keeps_original_tokens(self):
        self.apply([5])
        self.assertLessEqual(abs(len(self.eng.read(KO)) - len(self.orig[KO])), 8)

    def test_unimplemented_group_refused(self):
        with self.assertRaises(PatchError):
            self.apply([5, 100])
        self.assertEqual(self.eng.read(KO), self.orig[KO])

    @unittest.skipUnless(dll.available(), 'DLL 빌드 없음')
    def test_dll_flags(self):
        self.apply([1, 6, 7, 8, 9])
        with open(os.path.join(self.game, dll.CONFIG_NAME), encoding='utf-8') as f:
            lines = set(f.read().split())
        for k in ['expert_scaling', 'log_plus', 'post_busy', 'train_cost', 'even_develop']:
            self.assertIn(f'{k}=1', lines)
        for k in ['trait_balance', 'auto_search', 'trait_tooltip', 'fog_of_war']:
            self.assertIn(f'{k}=0', lines)
        self.assertEqual(self.eng.applied_ids(), [1, 6, 7, 8, 9])
        self.apply([])
        self.assertFalse(os.path.exists(os.path.join(self.game, dll.DLL_NAME)))
        self.assertFalse(os.path.exists(os.path.join(self.game, dll.CONFIG_NAME)))

    def test_dry_run_writes_nothing(self):
        self.eng.apply([5], dry_run=True, log=lambda *a: None)
        self.assertEqual(self.eng.read(KO), self.orig[KO])
        self.assertFalse(os.path.exists(os.path.join(self.game, core.STATE_DIR)))

    def test_state_file(self):
        self.apply([5])
        with open(os.path.join(self.game, core.STATE_DIR, core.STATE_FILE), encoding='utf-8') as f:
            state = json.load(f)
        self.assertEqual(state['groups'], ['formation'])
        self.assertEqual(state['files'][KO]['groups'], ['formation'])

    def test_schema1_state_migrates(self):
        # 2026-10-20 이전 패처는 그룹을 옛 번호로 기록했다(진형 = 1)
        self.apply([5])
        p = os.path.join(self.game, core.STATE_DIR, core.STATE_FILE)
        with open(p, encoding='utf-8') as f:
            state = json.load(f)
        state['schema'] = 1
        state['groups'] = [1]
        for rec in state['files'].values():
            rec['groups'] = [1] if rec['groups'] else []
        with open(p, 'w', encoding='utf-8') as f:
            json.dump(state, f)
        self.assertEqual(self.eng.applied_ids(), [5])
        self.assertTrue(self.eng.status(log=lambda *a: None))
        self.apply([])
        for rel, data in self.orig.items():
            self.assertEqual(self.eng.read(rel), data)


if __name__ == '__main__':
    unittest.main()

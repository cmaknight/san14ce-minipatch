"""패처 엔진: 원본 백업, 원본 기준 재생성, 상태 기록. 동작 규칙은 docs/00-overview.md."""
import ctypes
import datetime
import hashlib
import json
import os
import re
import struct

from . import dll, versions
from .groups import BY_ID, BY_KEY, GROUPS, LEGACY_IDS, PatchError
from .s14 import CODECS

STATE_DIR = '_san14patch'
STATE_FILE = 'state.json'
SCHEMA = 2  # 2: 그룹을 번호 대신 key로 기록


class UnknownFile(PatchError):
    """대상 파일이 알려진 원본도, 내용 확인으로도 원본이라 볼 수 없는 경우."""
    def __init__(self, rel, detail=''):
        self.rel = rel
        self.detail = detail  # 진단 로그용(크기, 해시, 내용 확인 실패 이유)
        super().__init__(
            f'원본과 다른 게임 파일이 있어 적용을 멈췄습니다: {rel}\n\n'
            '원인: 다른 모드·패치나 편집 도구가 이 파일을 바꿨거나, 파일이 손상된 것으로 보입니다.\n\n'
            '해결: 다른 모드를 쓰고 있다면 먼저 제거하세요. 그다음 Steam 라이브러리에서 삼국지14 → 속성 → '
            '설치된 파일 → "게임 파일 무결성 확인"을 한 뒤 다시 적용하세요. '
            '이 패처는 같은 파일을 고치는 다른 모드와 함께 쓸 수 없습니다. 게임 파일은 바꾸지 않았습니다.')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def running_games(folder):
    """folder 안의 SAN14*.exe로 실행 중인 프로세스의 전체 경로 목록(Windows 전용)."""
    if os.name != 'nt':
        return []
    from ctypes import wintypes as wt
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi')
    pids = (wt.DWORD * 4096)()
    got = wt.DWORD()
    psapi.EnumProcesses(pids, ctypes.sizeof(pids), ctypes.byref(got))
    k32.OpenProcess.restype = wt.HANDLE
    folder = os.path.normcase(os.path.abspath(folder))
    found = []
    for pid in pids[:got.value // ctypes.sizeof(wt.DWORD)]:
        h = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(1024)
        size = wt.DWORD(len(buf))
        if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            path = buf.value
            if (re.match(r'(?i)^san14.*\.exe$', os.path.basename(path))
                    and os.path.normcase(os.path.dirname(path)) == folder):
                found.append(path)
        k32.CloseHandle(h)
    return sorted(found)


def file_version(path):
    """Windows 실행 파일의 제품 버전 문자열('1.0.1.0'). 없거나 읽지 못하면 None."""
    if os.name != 'nt' or not os.path.isfile(path):
        return None
    ver = ctypes.WinDLL('version')
    size = ver.GetFileVersionInfoSizeW(path, None)
    if not size:
        return None
    buf = ctypes.create_string_buffer(size)
    if not ver.GetFileVersionInfoW(path, 0, size, buf):
        return None
    ptr, n = ctypes.c_void_p(), ctypes.c_uint()
    if not ver.VerQueryValueW(buf, '\\',ctypes.byref(ptr), ctypes.byref(n)) or n.value < 52:
        return None
    # VS_FIXEDFILEINFO: dwProductVersionMS/LS는 +16/+20
    ms, ls = struct.unpack_from('<II', ctypes.string_at(ptr.value, 52), 16)
    return f'{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}'


def check_game(folder):
    """GUI 상태 줄용 검사. 쓰기 없이 폴더를 판정한다.

    반환: {'ok': 적용 가능, 'message': 설명, 'product': 설치된 게임 버전, 'applied': 적용된 그룹 번호}
    """
    res = {'ok': False, 'message': '', 'product': None, 'applied': []}
    if not folder or not os.path.isdir(folder):
        res['message'] = '폴더가 없습니다. 게임 설치 폴더를 선택하세요'
        return res
    eng = Engine(folder)
    found, hashes = eng.version()
    exe = next((e for e in versions.EXES[1:] + versions.EXES[:1] if e in hashes), None)
    if not exe:
        res['message'] = '이 폴더에 삼국지14 CE 실행 파일(SAN14CE.exe)이 없습니다. 게임 설치 폴더를 선택하세요'
        return res
    res['product'] = file_version(eng.path(exe))
    if not found:
        cause, fix = version_advice(res['product'])
        res['message'] = f'지원하지 않는 게임 버전입니다(지원: Steam판 {versions.SUPPORTED}). {cause}.\n{fix}'
        return res
    try:
        res['applied'] = eng.applied_ids()
    except (PatchError, ValueError, KeyError) as e:
        res['message'] = f'패치 기록 파일({STATE_DIR}/{STATE_FILE})을 읽지 못했습니다. {STATE_DIR} 폴더를 지우고 Steam 무결성 확인 후 다시 적용하세요 ({e})'
        return res
    if running_games(folder):
        res['running'] = True
        res['message'] = '게임이 실행 중입니다. 게임을 종료하면 적용할 수 있습니다'
        return res
    res['ok'] = True
    res['message'] = '지원 버전 (' + ', '.join(versions.VERSIONS[k]['label'] for k in found) + ')'
    return res


class Engine:
    def __init__(self, game, trust_current=False, allow_blocked=False, data_only=False):
        self.game = os.path.abspath(game)
        self.data_only = data_only  # 개발용: DLL 없이 데이터 부분만 적용
        self.trust_current = trust_current
        self.allow_blocked = allow_blocked
        if not os.path.isdir(self.game):
            raise PatchError(f'게임 폴더가 없음: {self.game}')

    # --- 경로와 상태 ---

    def path(self, rel):
        return os.path.join(self.game, *rel.split('/'))

    def backup_path(self, rel):
        return os.path.join(self.game, STATE_DIR, 'backup', *rel.split('/'))

    def load_state(self):
        p = os.path.join(self.game, STATE_DIR, STATE_FILE)
        if not os.path.exists(p):
            return {'schema': SCHEMA, 'groups': [], 'files': {}, 'dll': None}
        with open(p, encoding='utf-8') as f:
            state = json.load(f)
        if state.get('schema') == 1:
            state = _migrate_v1(state)
        if state.get('schema') != SCHEMA:
            raise PatchError(f'상태 파일 형식이 다름: {p}')
        return state

    def applied_ids(self, state=None):
        """상태 파일에 기록된 적용 그룹의 현재 표시 번호."""
        state = state or self.load_state()
        return sorted(BY_KEY[k].id for k in state['groups'] if k in BY_KEY)

    def save_state(self, state):
        state['updated'] = datetime.datetime.now().isoformat(timespec='seconds')
        _write(os.path.join(self.game, STATE_DIR, STATE_FILE),
               json.dumps(state, ensure_ascii=False, indent=2).encode('utf-8'))

    def read(self, rel):
        p = self.path(rel)
        if not os.path.exists(p):
            return None
        with open(p, 'rb') as f:
            return f.read()

    # --- 판별 ---

    def version(self):
        hashes = {}
        for exe in versions.EXES:
            data = self.read(exe)
            if data is not None:
                hashes[exe] = sha256(data)
        return versions.identify(hashes), hashes

    def targets(self):
        """모든 구현 그룹의 수정 대상(선택 여부와 무관). 해제된 그룹의 파일도 원본으로 되돌려야 하므로."""
        rels = set()
        for g in GROUPS:
            if g.implemented:
                rels.update(g.targets(self.game))
        return sorted(rels)

    def inspect(self, rel, state):
        """파일 상태: missing / original / patched / external(패처가 모르는 내용)."""
        data = self.read(rel)
        if data is None:
            return 'missing'
        h = sha256(data)
        rec = state['files'].get(rel)
        if rec and h == rec.get('output') and h != rec['original']:
            return 'patched'
        if (rec and h == rec['original']) or h in versions.known_originals(rel):
            return 'original'
        return 'external'

    def base(self, rel, state):
        """rel의 원본 바이트와, 백업을 새로 써야 하면 True."""
        cur = self.read(rel)
        if cur is None:
            raise PatchError(f'게임 파일이 없습니다: {rel}\nSteam "게임 파일 무결성 확인"을 한 뒤 다시 적용하세요.')
        h = sha256(cur)
        rec = state['files'].get(rel)
        bp = self.backup_path(rel)
        if rec and os.path.exists(bp):
            with open(bp, 'rb') as f:
                bk = f.read()
            if sha256(bk) != rec['original']:
                raise PatchError(f'패치 백업 파일이 손상됐습니다: {bp}\n\n'
                                 f'게임 폴더의 {STATE_DIR} 폴더를 지우고, Steam "게임 파일 무결성 확인"을 한 뒤 다시 적용하세요.')
            if h in (rec['original'], rec.get('output')):
                return bk, False
            # Steam 무결성 검사나 (지원 목록에 추가된) 업데이트로 원본이 바뀐 경우: 현재 파일을 새 원본으로 본다
            if h in versions.known_originals(rel) or self.trust_current:
                return cur, True
            # 알 수 없는 내용(수동으로 넣은 실험 파일 등). 실행 파일 버전은 이미 확인했으므로 백업을 원본으로 쓴다
            return bk, False
        if h in versions.known_originals(rel) or self.trust_current:
            return cur, True
        reasons = []
        for g in GROUPS:
            if g.implemented:
                r = g.verify_unknown(rel, cur)
                if r is True:
                    return cur, True
                if r:
                    reasons.append(f'그룹 {g.id}: {r}')
        known = sorted(h[:16] for h in versions.known_originals(rel))
        raise UnknownFile(rel, f'{rel}: {len(cur):,} B, sha256 {h}, 알려진 원본 {known or "없음"}; '
                               + ('; '.join(reasons) or '내용 확인 방법 없음'))

    # --- 적용 ---

    def apply(self, ids, dry_run=False, log=print):
        """ids 그룹만 적용하고 나머지는 원본으로 되돌린다. 빈 목록이면 전체 원복.
        반환: 원본으로 확인하지 못해 건너뛴 시나리오 파일 목록(이 파일은 바꾸지 않는다)."""
        ids = sorted(set(ids))
        for i in ids:
            g = BY_ID.get(i)
            if g is None:
                raise PatchError(f'없는 그룹: {i}')
            if not g.implemented:
                raise PatchError(f'그룹 {i}({g.name})는 아직 구현되지 않았다')
            if g.blocked and not self.allow_blocked:
                raise PatchError(f'그룹 {i}({g.name}) 적용 불가: {g.blocked}')
        running = running_games(self.game)
        if running:
            raise PatchError('게임이 실행 중입니다. 게임을 완전히 종료한 뒤 다시 적용하세요.\n' + '\n'.join(running))
        found, hashes = self.version()
        if not hashes:
            raise PatchError(NO_EXE)
        if not found and not self.trust_current:
            raise PatchError(unsupported_message(self.game, hashes))
        log('게임 버전: ' + (', '.join(versions.VERSIONS[k]['label'] for k in found) or '미확인(--trust-current)'))

        state = self.load_state()
        selected = [BY_ID[i] for i in ids]
        need_dll = [g.name for g in selected if g.dll_flag]
        if need_dll and not self.data_only and not dll.available():
            raise PatchError(f'패처 파일 일부({dll.DLL_NAME})가 없습니다. 패처를 다시 받아 압축을 모두 푼 뒤 실행하세요.')
        plan = []  # (rel, 원본, 새 백업 필요, 출력, 적용 그룹)
        skipped = []
        for rel in self.targets():
            try:
                orig, new_backup = self.base(rel, state)
            except UnknownFile as e:
                log(f'[진단] {e.detail}')
                # 시나리오는 그 시나리오로 새 게임을 할 때만 쓰이므로 건너뛰고 나머지는 적용한다
                if not is_scenario(rel):
                    raise
                skipped.append(rel)
                log(f'건너뜀(원본과 다른 시나리오 파일, 바꾸지 않음): {rel}')
                continue
            groups = [g for g in selected if rel in g.targets(self.game)]
            if groups:
                ext = os.path.splitext(rel)[1].lower()
                doc = CODECS[ext](orig)
                for g in groups:
                    for c in g.patch(rel, doc):  # 목록 순서로 적용
                        log(f'  [{g.id}] {rel}: {c}')
                out = doc.to_bytes()
            else:
                out = orig
            plan.append((rel, orig, new_backup, out, [g.key for g in groups]))

        flags = {g.dll_flag: g.id in ids and not self.data_only for g in GROUPS if g.dll_flag}
        dll.check(self.game, state, flags)
        if dry_run:
            for rel, orig, _, out, gids in plan:
                cur = self.read(rel)
                act = '변경 없음' if cur == out else ('원본 복원' if out == orig else f'그룹 {_nums(gids)} 적용')
                log(f'{rel}: {act}')
            log(f'{dll.DLL_NAME}: {"설치/갱신, 켜짐 " + ",".join(k for k, v in flags.items() if v) if any(flags.values()) else "제거(설치돼 있으면)"}')
            return skipped

        for rel, orig, new_backup, out, gids in plan:
            if new_backup:
                _write(self.backup_path(rel), orig)
            if self.read(rel) != out:
                _write(self.path(rel), out)
                log(f'{rel}: {"원본 복원" if out == orig else f"그룹 {_nums(gids)} 적용"}')
            state['files'][rel] = {'original': sha256(orig), 'output': sha256(out), 'groups': gids}
        if self.data_only and need_dll:
            log(f'데이터만 적용(--data-only): DLL 기능 미설치 — {", ".join(need_dll)}')
        state['dll'] = dll.sync(self.game, state, flags, _write)
        on = [g.id for g in GROUPS if g.dll_flag and flags.get(g.dll_flag)]
        log(f'{dll.DLL_NAME}: ' + (f'설치, 그룹 {on} 기능 켬' if state['dll'] else '없음'))
        state['groups'] = [BY_ID[i].key for i in ids]
        self.save_state(state)
        return skipped

    def status(self, log=print):
        found, hashes = self.version()
        for exe, h in hashes.items():
            log(f'{exe}: {h[:16]}…')
        log(f'게임 버전: {", ".join(found) or "지원하지 않음"}')
        state = self.load_state()
        log(f'적용 기록: 그룹 {self.applied_ids(state) or "없음"} ({state.get("updated", "기록 없음")})')
        ok = True
        for rel in self.targets():
            st = self.inspect(rel, state)
            rec = state['files'].get(rel)
            want = rec['groups'] if rec else []
            log(f'  {rel}: {st}' + (f' (그룹 {_nums(want)})' if want else ''))
            if (st == 'patched') != bool(want) or st in ('external', 'missing'):
                ok = False
        rec = state.get('dll')
        path = self.path(dll.DLL_NAME)
        if rec:
            cur = sha256(open(path, 'rb').read()) if os.path.exists(path) else None
            st = 'installed' if cur == rec['sha'] else ('missing' if cur is None else 'external')
            log(f'  {dll.DLL_NAME}: {st}')
            ok = ok and st == 'installed'
        elif os.path.exists(path):
            log(f'  {dll.DLL_NAME}: 패처가 설치하지 않은 파일이 있음')
        if not ok:
            log('불일치: 파일 상태가 적용 기록과 다르다. Steam 무결성 검사나 업데이트로 바뀌었을 수 있으니 다시 적용할 것')
        return ok


NO_EXE = ('선택한 폴더에 삼국지14 CE 실행 파일(SAN14CE.exe)이 없습니다. 게임 설치 폴더를 선택하세요.\n'
          '찾는 법: Steam 라이브러리에서 삼국지14 → 관리 → 로컬 파일 보기')


def _ver(text):
    try:
        return tuple(int(x) for x in text.split('.'))[:3]
    except (AttributeError, ValueError):
        return None


def version_advice(product):
    """지원 목록에 없는 실행 파일일 때 (짧은 원인, 해결 방법). 설치된 제품 버전으로 경우를 나눈다."""
    have, want = _ver(product), _ver(versions.SUPPORTED)
    if have is None:
        return ('실행 파일 버전을 읽지 못했습니다',
                'Steam "게임 파일 무결성 확인"을 한 뒤 다시 시도하세요.')
    if have < want:
        return (f'이전 버전({product})이 설치돼 있습니다',
                'Steam에서 이전 버전으로 되돌렸다면(속성 → 베타 등) 최신 버전으로 되돌린 뒤 다시 시도하세요: '
                'Steam 라이브러리 → 삼국지14 → 속성 → 베타에서 "없음"을 고르고 업데이트가 끝날 때까지 기다리세요.')
    if have > want:
        return (f'게임이 새 버전({product})으로 업데이트됐습니다',
                '이 패처는 아직 새 버전을 지원하지 않습니다. 새 버전용 패처를 기다려 주세요.')
    return (f'버전({product})은 같지만 실행 파일이 원본과 다릅니다',
            '다른 모드·패치가 실행 파일을 바꾼 것으로 보입니다. 그 모드를 제거하고 '
            'Steam "게임 파일 무결성 확인"을 한 뒤 다시 시도하세요.')


def unsupported_message(game, hashes):
    exe = next((e for e in versions.EXES[1:] + versions.EXES[:1] if e in hashes), None)
    product = file_version(os.path.join(game, exe)) if exe else None
    cause, fix = version_advice(product)
    return (f'지원하지 않는 게임 버전입니다. 이 패처는 Steam판 {versions.SUPPORTED} 전용입니다.\n'
            f'원인: {cause}.\n\n{fix}')


def is_scenario(rel):
    return os.path.basename(rel).lower().startswith('scedaexce')


def _nums(keys):
    """상태 파일의 그룹 key 목록 → 표시 번호 목록."""
    return sorted(BY_KEY[k].id for k in keys if k in BY_KEY)


def _migrate_v1(state):
    """schema 1(그룹 번호 기록)을 key 기록으로 바꾼다. 파일에는 다음 적용 때 쓴다."""
    conv = lambda ids: [LEGACY_IDS[i] for i in ids if i in LEGACY_IDS]
    state = dict(state, schema=SCHEMA, groups=conv(state.get('groups', [])))
    state['files'] = {rel: dict(rec, groups=conv(rec.get('groups', [])))
                      for rel, rec in state.get('files', {}).items()}
    return state


def _write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + '.san14patch.tmp'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, path)

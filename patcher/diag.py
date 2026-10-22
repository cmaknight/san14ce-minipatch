"""제보용 진단 정보. 적용 실패나 "로그 복사" 때 게임 폴더와 환경을 읽기만 해서 요약한다(아무것도 쓰지 않음)."""
import glob
import hashlib
import json
import os
import platform
import sys

from . import core, dll, versions

# 게임 폴더에 있으면 다른 프록시 DLL·후킹 도구일 수 있는 이름
PROXY_NAMES = ['dinput8.dll', 'dxgi.dll', 'd3d11.dll', 'd3d9.dll', 'd3d12.dll', 'version.dll', 'winmm.dll',
               'dsound.dll', 'xinput1_3.dll', 'xinput1_4.dll', 'opengl32.dll', 'winhttp.dll', 'ReShade.ini']


def _sha(path, n=16):
    try:
        with open(path, 'rb') as f:
            return hashlib.sha256(f.read()).hexdigest()[:n]
    except OSError as e:
        return f'읽기 실패({e.strerror})'


def _size(path):
    try:
        return f'{os.path.getsize(path):,} B'
    except OSError:
        return '?'


def _is_admin():
    try:
        import ctypes
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return None


def collect(folder):
    """진단 줄 목록. 각 항목에서 예외가 나도 나머지는 계속 모은다."""
    out = []

    def section(title, fn):
        try:
            fn()
        except Exception as e:
            out.append(f'{title}: 수집 실패 {type(e).__name__}: {e}')

    def env():
        out.append(f'패처: v{versions.PATCHER_VERSION}, 지원 게임 {versions.SUPPORTED}, 실행 파일 {"배포판" if getattr(sys, "frozen", False) else "개발"}, '
                   f'내장 DLL {_sha(dll.BUILD) if dll.available() else "없음"}')
        out.append(f'Windows: {platform.platform()}, 관리자 권한 {_is_admin()}')
        out.append(f'게임 폴더: {folder} (존재 {os.path.isdir(folder)})')
    section('환경', env)
    if not os.path.isdir(folder):
        return out

    def exes():
        for exe in versions.EXES:
            p = os.path.join(folder, exe)
            if os.path.exists(p):
                h = _sha(p, 64)
                ok = [k for k, v in versions.VERSIONS.items() if v['files'].get(exe) == h]
                out.append(f'  {exe}: {_size(p)}, 제품 버전 {core.file_version(p)}, sha256 {h[:16]}, '
                           f'{"지원 " + ",".join(ok) if ok else "알 수 없는 해시"}')
        running = core.running_games(folder)
        if running:
            out.append(f'  실행 중: {running}')
    section('실행 파일', exes)

    def state():
        p = os.path.join(folder, core.STATE_DIR, core.STATE_FILE)
        if not os.path.exists(p):
            out.append('패치 기록: 없음(처음 적용)')
            return
        with open(p, encoding='utf-8') as f:
            st = json.load(f)
        changed = sum(1 for r in st.get('files', {}).values() if r.get('groups'))
        out.append(f'패치 기록: schema {st.get("schema")}, 그룹 {st.get("groups")}, 갱신 {st.get("updated")}, '
                   f'파일 {len(st.get("files", {}))}개(바뀐 것 {changed}), DLL {st.get("dll")}')
    section('패치 기록', state)

    def proxies():
        found = []
        for n in PROXY_NAMES:
            p = os.path.join(folder, n)
            if os.path.exists(p):
                found.append(f'{n}({_size(p)}, sha256 {_sha(p)}, 버전 {core.file_version(p)})')
        found += [os.path.basename(p) for p in glob.glob(os.path.join(folder, '*.asi'))]
        out.append('게임 폴더의 DLL·후킹 파일: ' + (', '.join(found) or '없음'))
    section('DLL', proxies)

    def data():
        eng = core.Engine(folder)
        try:
            st = eng.load_state()
        except Exception:
            st = {'files': {}}
        by_dir = {}
        for rel in eng.targets():
            d, name = rel.rsplit('/', 1)
            # original = 알려진 원본(또는 기록된 원본), patched = 이 패처의 출력, external = 그 밖(진단 대상)
            by_dir.setdefault(d, []).append((name, eng.inspect(rel, st)))
        for d, items in sorted(by_dir.items()):
            count = {}
            for _, s in items:
                count[s] = count.get(s, 0) + 1
            line = f'  {d}: 대상 {len(items)}개 ' + ', '.join(f'{k} {v}' for k, v in sorted(count.items()))
            ext = [n for n, s in items if s == 'external']
            if ext:
                line += ' (external: ' + ', '.join(ext[:12]) + (' …' if len(ext) > 12 else '') + ')'
            baks = len(glob.glob(os.path.join(folder, *d.split('/'), '*.bak*')))
            if baks:
                line += f', *.bak {baks}개'
            out.append(line)
        nums = sorted({n for d, items in by_dir.items() if d.startswith('DLC/') for n, _ in items})
        out.append(f'  DLC 시나리오: {", ".join(nums) or "없음"}')
        dlc = sorted(os.path.basename(p) for p in glob.glob(os.path.join(folder, 'DLC', 'DLCInfo*.s14')))
        out.append(f'  DLC 정보 파일: {", ".join(dlc) or "없음"}')
    section('데이터 파일', data)
    return out

"""Steam 설치 폴더에서 삼국지14 CE 찾기(Windows 레지스트리 + libraryfolders.vdf)."""
import os
import re

APP_ID = '872410'  # 삼국지14 (Steam)
INSTALL_DIR = 'Romance_of_the_Three_Kingdoms_14'
DEFAULT = os.path.join(r'C:\Program Files (x86)\Steam\steamapps\common', INSTALL_DIR)


def is_game_dir(path):
    """SAN14CE*.exe가 있는 폴더인지."""
    try:
        return any(re.match(r'(?i)^san14ce.*\.exe$', n) for n in os.listdir(path))
    except OSError:
        return False


def _steam_roots():
    roots = []
    try:
        import winreg
    except ImportError:
        return roots
    # HKLM 값이 대소문자를 보존해 먼저 본다(HKCU SteamPath는 소문자)
    for hive, key, val in [(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath'),
                           (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Valve\Steam', 'InstallPath'),
                           (winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', 'SteamPath')]:
        try:
            with winreg.OpenKey(hive, key) as k:
                roots.append(os.path.normpath(winreg.QueryValueEx(k, val)[0]))
        except OSError:
            pass
    return roots


def _vdf_values(path, name):
    """vdf 파일에서 "name" "값" 꼴의 값 목록(중첩 구조는 보지 않는다)."""
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError:
        return []
    pat = r'"%s"\s+"((?:[^"\\]|\\.)*)"' % re.escape(name)
    return [v.replace('\\\\', '\\') for v in re.findall(pat, text)]


def libraries():
    """Steam 라이브러리 폴더 목록(중복 제거, 발견 순서)."""
    libs = []
    for root in _steam_roots():
        libs.append(root)
        libs += _vdf_values(os.path.join(root, 'steamapps', 'libraryfolders.vdf'), 'path')
    seen, out = set(), []
    for p in libs:
        key = os.path.normcase(os.path.normpath(p))
        if key not in seen:
            seen.add(key)
            out.append(os.path.normpath(p))
    return out


def find_game():
    """설치된 게임 폴더 후보 목록. 못 찾으면 빈 목록."""
    found = []
    for lib in libraries():
        apps = os.path.join(lib, 'steamapps')
        names = _vdf_values(os.path.join(apps, f'appmanifest_{APP_ID}.acf'), 'installdir') or [INSTALL_DIR]
        for n in names:
            p = os.path.join(apps, 'common', n)
            if is_game_dir(p) and p not in found:
                found.append(p)
    return found


def default_game():
    """GUI 기본값: 찾은 첫 폴더, 없으면 기본 설치 경로."""
    found = find_game()
    return found[0] if found else DEFAULT

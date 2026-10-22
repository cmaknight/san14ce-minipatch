"""프록시 DLL 설치·제거와 설정 파일. DLL 소스는 src/dll/, 빌드는 src/dll/build.cmd → out/dll/dinput8.dll."""
import hashlib
import os
import sys

from .groups import PatchError

DLL_NAME = 'dinput8.dll'
CONFIG_NAME = 'san14patch.ini'
if getattr(sys, 'frozen', False):
    # 배포용 실행 파일(PyInstaller): 묶어 넣은 DLL
    BUILD = os.path.join(sys._MEIPASS, DLL_NAME)
else:
    BUILD = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out', 'dll', DLL_NAME)


def _sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def available():
    """설치할 DLL 본체(빌드 결과)가 있는지."""
    return os.path.isfile(BUILD)


def check(game, state, flags):
    """쓰기 전에 부르는 검사. 우리가 설치하지 않은 같은 이름의 DLL이 있으면 덮어쓰거나 지우지 않는다."""
    target = os.path.join(game, DLL_NAME)
    if not os.path.exists(target):
        return
    ours = {(state.get('dll') or {}).get('sha')}
    if available():
        ours.add(_sha(BUILD))
    if _sha(target) not in ours:
        if any(flags.values()):
            raise PatchError(f'게임 폴더에 이 패처가 설치하지 않은 {DLL_NAME}이 있어 적용을 멈췄습니다. '
                            'ReShade 같은 다른 프로그램이나 모드의 파일일 수 있습니다. '
                            f'그 파일을 다른 곳으로 옮긴 뒤 다시 적용하세요(이 패처도 같은 이름의 파일을 씁니다). 게임 파일은 바꾸지 않았습니다.')


def write_config(game, flags):
    lines = ['[groups]'] + [f'{k}={1 if v else 0}' for k, v in sorted(flags.items())]
    with open(os.path.join(game, CONFIG_NAME), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')


def sync(game, state, flags, write):
    """flags: DLL 기능 {키: 켜짐}. write(path, bytes)는 엔진의 원자적 쓰기. 반환값은 state['dll']."""
    target = os.path.join(game, DLL_NAME)
    rec = state.get('dll') or {}
    if not any(flags.values()):
        if rec and os.path.exists(target) and _sha(target) == rec.get('sha'):
            os.remove(target)
        cfg = os.path.join(game, CONFIG_NAME)
        if rec and os.path.exists(cfg):
            os.remove(cfg)
        return None
    if not available():
        raise PatchError(f'패처 파일 일부({DLL_NAME})가 없습니다. 패처를 다시 받아 압축을 모두 푼 뒤 실행하세요.')
    with open(BUILD, 'rb') as f:
        data = f.read()
    sha = hashlib.sha256(data).hexdigest()
    if not os.path.exists(target) or _sha(target) != sha:
        write(target, data)
    write_config(game, flags)
    return {'name': DLL_NAME, 'sha': sha}

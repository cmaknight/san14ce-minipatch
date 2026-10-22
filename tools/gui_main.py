"""배포용 실행 파일 진입점(PyInstaller). 빌드는 tools/build_gui.cmd.

빌드 확인용: `San14CE_MiniPatch.exe --selftest <게임 폴더> <결과 파일> [그룹 번호...]`
묶인 실행 파일 안에서 엔진(DLL·해시 목록 경로 포함)으로 적용하고 로그를 결과 파일에 쓴다.
"""
import sys


def selftest(folder, out, ids):
    from patcher import core
    lines = []
    try:
        core.Engine(folder).apply([int(i) for i in ids], log=lines.append)
        lines.append('SELFTEST OK')
    except Exception as e:
        lines.append(f'SELFTEST FAIL {type(e).__name__}: {e}')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    return 0


if len(sys.argv) >= 4 and sys.argv[1] == '--selftest':
    sys.exit(selftest(sys.argv[2], sys.argv[3], sys.argv[4:]))

from patcher.gui import main  # noqa: E402

sys.exit(main())

import argparse
import os
import sys

from .core import Engine
from .groups import GROUPS, PatchError


def main(argv=None):
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
    ap = argparse.ArgumentParser(prog='san14patch', description='삼국지14 CE Mini 패치 관리')
    ap.add_argument('--game', default=os.environ.get('SAN14_DIR'),
                    help='게임 설치 폴더(기본값: 환경 변수 SAN14_DIR)')
    ap.add_argument('--trust-current', action='store_true',
                    help='해시를 모르는 파일도 원본으로 보고 진행(업데이트 직후 개발용)')
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('groups', help='패치 그룹 목록')
    sub.add_parser('status', help='게임 버전과 적용 상태 확인')
    p = sub.add_parser('apply', help='지정한 그룹만 적용, 나머지는 원본으로')
    p.add_argument('ids', nargs='*', type=int)
    p.add_argument('-n', '--dry-run', action='store_true')
    p.add_argument('--data-only', action='store_true',
                   help='DLL 기능은 빼고 데이터 수정만 적용(DLL 완성 전 시험용)')
    sub.add_parser('restore', help='전체 원복(apply와 그룹 없음이 같음)')
    args = ap.parse_args(argv)

    if args.cmd == 'groups':
        for g in GROUPS:
            tag = '  (미구현)' if not g.implemented else ('  (적용 불가)' if g.blocked else '')
            print(f'{g.id}  {g.label}{tag}')
        return 0
    if not args.game:
        ap.error('--game 또는 SAN14_DIR이 필요하다')
    try:
        eng = Engine(args.game, trust_current=args.trust_current,
                     data_only=getattr(args, 'data_only', False))
        if args.cmd == 'status':
            return 0 if eng.status() else 1
        ids = args.ids if args.cmd == 'apply' else []
        eng.apply(ids, dry_run=getattr(args, 'dry_run', False))
        print('완료')
        return 0
    except PatchError as e:
        print(f'오류: {e}', file=sys.stderr)
        return 2

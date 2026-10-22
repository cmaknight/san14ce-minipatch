class PatchError(Exception):
    pass


class Group:
    """패치 그룹. 데이터 그룹은 targets/patch를, DLL 그룹은 dll_flag를 정의한다."""
    id = 0
    key = ''  # 상태 파일에 남는 고정 이름
    name = ''  # 패처 제목(체크박스 글자)
    desc = ''  # 제목 아래 작은 글씨 설명. 한 줄 넘으면 \n으로 나눈다
    new_game = False  # 진행 중인 세이브에는 반영되지 않고 새 게임부터 적용
    implemented = False
    blocked = None  # 적용을 막는 이유. 대상 파일은 계속 원본 복원 대상이다
    dll_flag = None  # 설정 파일에 쓸 키. None이면 DLL 기능 없음

    @property
    def label(self):
        return self.name + (' (새 게임 필요)' if self.new_game else '')

    def targets(self, game):
        """수정할 파일의 게임 폴더 기준 상대 경로 목록(존재하는 것만)."""
        return []

    def verify_unknown(self, rel, data):
        """해시 목록에 없는 대상 파일을 내용으로 판정한다(우리가 갖지 않은 DLC 등).
        판정 대상이 아니면 None, 원본이면 True, 아니면 이유 문자열."""
        return None

    def patch(self, rel, doc):
        """doc(코덱 객체)을 제자리에서 고치고 변경 설명 문자열 목록을 반환한다."""
        return []


def find_unique(buf, pattern, what):
    p = buf.find(pattern)
    if p < 0:
        raise PatchError(f'{what}: 패턴을 찾지 못함')
    if buf.find(pattern, p + 1) >= 0:
        raise PatchError(f'{what}: 패턴이 여러 곳에서 발견됨')
    return p

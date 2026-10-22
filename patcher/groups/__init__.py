"""패치 그룹 목록. 표시 번호(id)와 적용 순서는 목록 순서다(docs/00-overview.md).

key는 상태 파일(state.json)에 남는 고정 이름이라 번호를 바꿔도 그대로 둔다.
"""
from .base import Group, PatchError
from .formation import Formation
from .traits import Traits


class _DllOnly(Group):
    """데이터 파일 없이 프록시 DLL 기능만 켜는 그룹."""
    implemented = True

    def __init__(self, gid, key, name, desc, dll_flag):
        self.id = gid
        self.key = key
        self.name = name
        self.desc = desc
        self.dll_flag = dll_flag


class _Planned(_DllOnly):
    implemented = False


GROUPS = [
    _DllOnly(1, 'expert', '숙련 모드 보강',
             '부대능력 배율(50~200%) 추가, 인접지역효과 최대치 2000%로, 무장경험획득 최소치 0%', 'expert_scaling'),
    Traits(),
    _DllOnly(3, 'autosearch', '페이즈 종료시 자동 탐색',
             '자동진행 켜 두면 페이즈 종료시 남은 명령서로 탐색 자동 실행', 'auto_search'),
    _DllOnly(4, 'tooltip', '개성·전법 툴팁 개선',
             '정보 목록 등에서 개성·전법 툴팁 표시 추가, 전법 효과 수치와 범위 표시 추가', 'trait_tooltip'),
    Formation(),
    _DllOnly(6, 'logs', '로그 기능 보강',
             '로그 창 드래그 크기 조절 및 휠 스크롤 추가\n'
             '모든 세력 장수의 사망/이벤트 포로/직할 지역 내정 Lv 상승 로그 추가', 'log_plus'),
    _DllOnly(7, 'post', '관직 임명 완화',
             '이동 중이거나 임무 중인 장수도 관직에 임명·교체·해임 가능 (출진 장수는 제외)', 'post_busy'),
    _DllOnly(8, 'traincost', '훈련비 비례 차감', '훈련비 지출이 실제로 오른 사기에 비례하도록 수정', 'train_cost'),
    _DllOnly(9, 'develop', '내정 AI 개선',
             'AI 군주/군단이 내정 Lv이 균등하게 오르도록 수정\n'
             '먼저 올리는 항목은 중시 설정을 따르며, 없으면 군주/도독의 성향을 따름', 'even_develop'),
    _Planned(100, 'fog', '정보 단계', '', 'fog_of_war'),  # 미구현·임시. GUI에 안 보이고 번호를 비켜 둔다
]
BY_ID = {g.id: g for g in GROUPS}
BY_KEY = {g.key: g for g in GROUPS}
# 상태 파일 schema 1의 그룹 번호(2026-10-20 이전 순서) → key
LEGACY_IDS = {1: 'formation', 2: 'traits', 3: 'expert', 4: 'autosearch', 5: 'tooltip', 6: 'fog'}

__all__ = ['GROUPS', 'BY_ID', 'BY_KEY', 'LEGACY_IDS', 'Group', 'PatchError']

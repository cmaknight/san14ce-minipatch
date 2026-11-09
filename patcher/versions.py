"""지원 게임 버전."""
import json
import os

# GUI 제목의 "(X 버전 패치 대응)". 지원 버전을 추가하면 같이 올린다
SUPPORTED = '1.0.2'
# 패처 자체 버전(창 제목, 배포 zip 이름). 배포할 때 올린다
PATCHER_VERSION = '1.4'

VERSIONS = {
    'B': {
        'label': '공식 1.0.2',
        'files': {
            'SAN14CE.exe': 'e18bfbb4eeb34195b79e12f096396eb5cee556e3174e549804702544187a325b',
            'SAN14CE_KO.exe': '782cf229a56cdde2b95399e0e7536740b3f5e96bea702679365e4dad061abdce',
            'SAN14CE_SC.exe': '2e49348aa1b9be5cfca651e71c8d77b2e4bd2d11387edc0b0236f4352458a239',
            'SAN14CE_TC.exe': 'e4be0f6c0ea3feec02f05e725a7a6be84d70cd1cc0bb96931a3656fbac125810',
            '0010_JP/fixdataexce.s14': '849d0e88501b80a4f0a04f8674739f9119be19d8b6006aaa592cd1105e8b1134',
            '0010_KO/fixdataexce.s14': 'bbcf4f5023b9e870f0c51e6f37b0f95401abe6e32ec29fddd75fdba52d4c07b7',
            '0010_SC/fixdataexce.s14': 'e158a7bac71969119780afc819415da6d15efb693c62aa997f77a7a22f222d1c',
            '0010_TC/fixdataexce.s14': '678eaabddd2c400e1a3412b500e521c478d749f7853d72ca4b7112d4040eb93f',
        },
    },
}
# 1.0.1(버전 A)은 v1.3부터 지원하지 않는다. 1.0.1에서 적용한 설치 폴더의 옛 백업은
# core.base()가 현재 파일(1.0.2 원본)을 새 원본으로 보고 백업을 다시 쓴다.

# 시나리오처럼 개수가 많은 대상 파일의 원본 해시. 원본 설치 폴더의 파일 SHA-256을 모아 만든다.
KNOWN_FILES = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'known_files.json')
if os.path.exists(KNOWN_FILES):
    with open(KNOWN_FILES, encoding='utf-8') as _f:
        for _key, _files in json.load(_f).items():
            VERSIONS[_key]['files'].update(_files)

EXES = ['SAN14CE.exe', 'SAN14CE_KO.exe', 'SAN14CE_SC.exe', 'SAN14CE_TC.exe']


def known_originals(rel):
    """rel 경로 파일의 알려진 원본 해시 집합."""
    return {v['files'][rel] for v in VERSIONS.values() if rel in v['files']}


def identify(hashes):
    """{상대 경로: sha256}에서 실행 파일이 모두 일치하는 버전 키 목록."""
    exes = {k: h for k, h in hashes.items() if k in EXES and h}
    if not exes:
        return []
    return [key for key, v in VERSIONS.items()
            if all(v['files'].get(k) == h for k, h in exes.items())]

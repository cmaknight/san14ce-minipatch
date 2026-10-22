"""지원 게임 버전. 버전별 값은 docs/00-overview.md."""
import json
import os

# GUI 제목의 "(X 버전 패치 대응)". 지원 버전을 추가하면 같이 올린다
SUPPORTED = '1.0.1'
# 패처 자체 버전(창 제목, 배포 zip 이름). 배포할 때 올린다
PATCHER_VERSION = '1.2'

VERSIONS = {
    'A': {
        'label': '공식 1.0.1',
        'files': {
            'SAN14CE.exe': 'f4b8268eedc0aa5f21efa9597d2bb4d6045b185fabcea740fed4ad6f1a163030',
            'SAN14CE_KO.exe': '87ed4e46590e0939c9efcaef14fa0f892917604dfda86e0a4be319fccffa2d6a',
            'SAN14CE_SC.exe': 'ceee6a94eb939b40c329b8ec5aecd938e9d3a51a6663e8e7d27a1498627afb88',
            'SAN14CE_TC.exe': '0751b4fed7a5783371b9541674c0587a6afd3140e60e706e32eaf35a9432f464',
            '0010_JP/fixdataexce.s14': '7810ddea9005b62e4b450bea5b8b98bdbc579016e66e854a7010d4b3b63f644c',
            '0010_KO/fixdataexce.s14': 'd3fac398d257407dcd81b0620bfd23f0956c3382c8926a5bd7ee4913da105a25',
            '0010_SC/fixdataexce.s14': 'e5467c718534597927b8e21d572ddd947f65961edb359c9dc5d2ebc492e1ebba',
            '0010_TC/fixdataexce.s14': 'e5da4d26b194f1268278106b47228a1fb631f1e91d83c2c99605bab4a035e0a4',
        },
    },
}

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

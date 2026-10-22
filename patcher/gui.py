"""배포용 GUI. 게임 폴더 선택, 그룹 체크박스, 적용(선택 없음 = 전체 원복), 원본 복구. 엔진은 core.Engine.apply 그대로."""
import datetime
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter import font as tkfont
from tkinter.scrolledtext import ScrolledText

from . import core, diag, steam, versions
from .groups import GROUPS, PatchError

TITLE = f'삼국지14 CE Mini 패치 ({versions.SUPPORTED} 버전 패치 대응) v{versions.PATCHER_VERSION}'
FONT = ('Malgun Gothic', 10)
DESC_FONT = (FONT[0], 9)
OK_COLOR, BAD_COLOR, DESC_COLOR = '#1a7f37', '#cf222e', '#57606a'
TABLE_MIN_H, LOG_MIN_H, WRAP_SLACK_H = 110, 56, 70  # 최소 크기에서 표·로그 높이, 줄바꿈 여유
GRID_COLOR, HEAD_BG, CELL_BG = '#c8ccd1', '#eef1f4', '#ffffff'  # 패치 표 경계선·머리글·칸


class App:
    def __init__(self, root):
        self.root = root
        self.groups = [g for g in GROUPS if g.implemented]
        self.vars = {g.id: tk.BooleanVar() for g in self.groups}
        self.logq = queue.Queue()
        self.busy = False
        self.pending = None

        root.title(TITLE)
        root.option_add('*Font', FONT)
        ttk.Style().configure('.', font=FONT)
        self.frm = frm = ttk.Frame(root, padding=14)
        frm.pack(fill='both', expand=True)
        frm.columnconfigure(0, weight=1)

        autowrap(tk.Label(frm, text=TITLE, font=(FONT[0], 14, 'bold'), anchor='w', justify='left')).grid(
            row=0, column=0, sticky='ew', pady=(0, 10))

        box = ttk.LabelFrame(frm, text='게임 폴더', padding=10)
        box.grid(row=1, column=0, sticky='ew')
        box.columnconfigure(0, weight=1)
        self.folder = tk.StringVar(value=steam.default_game())
        ent = ttk.Entry(box, textvariable=self.folder, width=72)
        ent.grid(row=0, column=0, sticky='ew')
        ttk.Button(box, text='찾아보기…', command=self.browse).grid(row=0, column=1, padx=(6, 0))
        ttk.Button(box, text='자동 찾기', command=self.autodetect).grid(row=0, column=2, padx=(6, 0))
        self.status = tk.Label(box, text='', anchor='w', justify='left')
        self.status.grid(row=1, column=0, columnspan=3, sticky='ew', pady=(6, 0))
        self.folder.trace_add('write', lambda *a: self.schedule_check())

        self.table_box = box = ttk.LabelFrame(frm, text='적용할 패치', padding=10)
        box.grid(row=2, column=0, sticky='nsew', pady=(10, 0))
        box.columnconfigure(0, weight=1)
        box.rowconfigure(0, weight=1)
        self.build_table(box)
        row = ttk.Frame(box)
        row.grid(row=1, column=0, columnspan=2, sticky='w', pady=(8, 0))
        ttk.Button(row, text='모두 선택', command=lambda: self.set_all(True)).pack(side='left')
        ttk.Button(row, text='모두 해제', command=lambda: self.set_all(False)).pack(side='left', padx=(6, 0))
        autowrap(tk.Label(box, text='선택하지 않은 패치는 원본으로 되돌립니다. 하나도 선택하지 않으면 모두 원본으로 돌아갑니다.',
                          fg=DESC_COLOR, anchor='w', justify='left')).grid(row=2, column=0, columnspan=2, sticky='ew',
                                                                          pady=(6, 0))

        row = ttk.Frame(frm)
        row.grid(row=3, column=0, sticky='ew', pady=(10, 0))
        ttk.Button(row, text='로그 복사', command=self.copy_log).pack(side='left')
        self.apply_btn = ttk.Button(row, text='적용', command=self.apply)
        self.apply_btn.pack(side='right', ipadx=20)
        self.restore_btn = ttk.Button(row, text='원본 복구', command=self.restore)
        self.restore_btn.pack(side='right', padx=(0, 6))

        self.log = ScrolledText(frm, height=6, state='disabled', font=('Consolas', 9), wrap='none')
        self.log.grid(row=4, column=0, sticky='nsew', pady=(10, 0))
        frm.rowconfigure(2, weight=3)
        frm.rowconfigure(4, weight=1)
        autowrap(self.status)

        self.set_all(True)  # 실행할 때는 모두 선택된 상태로 시작
        self.check()
        self.root.after(100, self.drain)

    def min_size(self):
        """창 최소 크기: 처음 크기의 절반. 표는 2행, 로그는 3줄 남짓까지 줄고 나머지는 스크롤한다.
        세로는 그 높이가 안 들어가면 그만큼 키운다(제목·상태 줄·안내가 줄바꿈되는 몫 포함)."""
        self.root.update_idletasks()
        cut_t = max(0, self.canvas.winfo_reqheight() - TABLE_MIN_H)
        cut_l = max(0, self.log.winfo_reqheight() - LOG_MIN_H)
        self.frm.rowconfigure(2, weight=3, minsize=self.table_box.winfo_reqheight() - cut_t)
        self.frm.rowconfigure(4, weight=1, minsize=self.log.winfo_reqheight() - cut_l + 10)
        w, h = self.root.winfo_reqwidth(), self.root.winfo_reqheight()
        return w // 2, max(h // 2, h - cut_t - cut_l + WRAP_SLACK_H)

    # --- 패치 표 ---

    def build_table(self, box):
        """그룹 표: 선택 · 번호 · 패치 이름 · 설명. 스크롤되는 캔버스 안에 칸마다 라벨을 두고, 칸 사이 1px 틈으로
        경계선을 그린다. 행 높이는 그 행 설명의 줄 수를 따른다. 기본 크기는 모든 행과 설명 원문 줄이 그대로 보이게,
        창을 줄이면 설명 칸이 줄어 설명이 줄바꿈되고(그 행만 높아짐) 넘치는 행은 세로 스크롤한다.
        행 어디를 눌러도 선택이 바뀐다."""
        font = tkfont.Font(font=FONT)
        pad = 8
        self.fixed_w = [52, 48, max(font.measure(g.label) for g in self.groups) + 2 * pad]
        self.desc_w = max(font.measure(x) for g in self.groups for x in g.desc.split('\n')) + 2 * pad + 8

        self.canvas = canvas = tk.Canvas(box, highlightthickness=1, highlightbackground=GRID_COLOR, bd=0, bg=GRID_COLOR)
        sb = ttk.Scrollbar(box, orient='vertical', command=canvas.yview)
        canvas.configure(yscrollcommand=sb.set)
        canvas.grid(row=0, column=0, sticky='nsew')
        sb.grid(row=0, column=1, sticky='ns')
        inner = tk.Frame(canvas, bg=GRID_COLOR)
        win = canvas.create_window(0, 0, window=inner, anchor='nw')

        for c, w in enumerate(self.fixed_w):
            inner.columnconfigure(c, minsize=w)
        inner.columnconfigure(3, weight=1)
        for c, text in enumerate(['선택', '번호', '패치 이름', '설명']):
            tk.Label(inner, text=text, font=(FONT[0], 10, 'bold'), bg=HEAD_BG, anchor='center' if c < 2 else 'w',
                     padx=pad, pady=4).grid(row=0, column=c, sticky='nsew', padx=(0 if c == 0 else 1, 0), pady=(0, 1))
        self.desc_labels = []
        for r, g in enumerate(self.groups, start=1):
            v = self.vars[g.id]
            cells = [tk.Checkbutton(inner, variable=v, bg=CELL_BG, activebackground=CELL_BG, bd=0,
                                    highlightthickness=0),
                     tk.Label(inner, text=str(g.id), bg=CELL_BG, anchor='center', padx=pad, pady=6),
                     tk.Label(inner, text=g.label, bg=CELL_BG, anchor='w', padx=pad, pady=6),
                     tk.Label(inner, text=g.desc, bg=CELL_BG, anchor='w', justify='left', padx=pad, pady=6,
                              wraplength=self.desc_w - 2 * pad)]
            for c, w in enumerate(cells):
                w.grid(row=r, column=c, sticky='nsew', padx=(0 if c == 0 else 1, 0), pady=(0, 1))
                if c:
                    w.bind('<Button-1>', lambda e, v=v: v.set(not v.get()))
            self.desc_labels.append(cells[3])

        def fit(e):
            # 캔버스 폭 = 표 폭. 설명 칸 = 나머지 폭, 그 폭으로 설명을 줄바꿈한다
            canvas.itemconfigure(win, width=e.width)
            wrap = max(120, e.width - sum(self.fixed_w) - 3 - 2 * pad)
            for lb in self.desc_labels:
                if lb.cget('wraplength') != wrap:
                    lb.configure(wraplength=wrap)

        canvas.bind('<Configure>', fit)
        inner.bind('<Configure>', lambda e: canvas.configure(scrollregion=canvas.bbox('all')))
        wheel = lambda e: canvas.yview_scroll(-1 if e.delta > 0 else 1, 'units')
        canvas.bind('<Enter>', lambda e: canvas.bind_all('<MouseWheel>', wheel))
        canvas.bind('<Leave>', lambda e: canvas.unbind_all('<MouseWheel>'))
        canvas.configure(yscrollincrement=20)
        # 기본 크기: 표 전체가 보이게
        inner.update_idletasks()
        canvas.configure(width=sum(self.fixed_w) + self.desc_w + 3, height=inner.winfo_reqheight())

    # --- 폴더 ---

    def browse(self):
        d = filedialog.askdirectory(title='삼국지14 설치 폴더 선택',
                                    initialdir=self.folder.get() if os.path.isdir(self.folder.get()) else None)
        if d:
            self.folder.set(os.path.normpath(d))

    def autodetect(self):
        found = steam.find_game()
        if found:
            self.folder.set(found[0])
        else:
            messagebox.showinfo(TITLE, 'Steam 라이브러리에서 삼국지14를 찾지 못했습니다. 찾아보기로 직접 선택하세요.')

    def schedule_check(self):
        if self.pending:
            self.root.after_cancel(self.pending)
        self.pending = self.root.after(300, self.check)

    def check(self, sync=False):
        """폴더 상태 줄 갱신. sync이면(적용·원본 복구 뒤) 선택을 현재 적용 상태로 맞춘다."""
        self.pending = None
        folder = self.folder.get().strip()
        try:
            res = core.check_game(folder)
        except Exception as e:  # 권한 등 예상 못 한 오류도 상태 줄에 보인다
            res = {'ok': False, 'message': f'검사 실패: {e}', 'product': None, 'applied': []}
        parts = [('✔ ' if res['ok'] else '✘ ') + res['message']]
        if res['product']:
            parts.append(f'설치된 게임: {res["product"]}')
        if res['ok'] or res['applied']:
            parts.append('현재 적용: ' + (', '.join(map(str, res['applied'])) or '없음(원본)'))
        self.status.config(text='   ·   '.join(parts), fg=OK_COLOR if res['ok'] else BAD_COLOR)
        self.ok = res['ok']
        if res.get('running') and not self.pending:  # 게임을 끄면 자동으로 다시 검사
            self.pending = self.root.after(2000, self.check)
        if sync:
            for gid, v in self.vars.items():
                v.set(gid in res['applied'])
        self.update_button()

    def update_button(self):
        st = 'normal' if self.ok and not self.busy else 'disabled'
        self.apply_btn.config(state=st)
        self.restore_btn.config(state=st)

    # --- 적용 ---

    def set_all(self, on):
        for v in self.vars.values():
            v.set(on)

    def apply(self):
        ids = [gid for gid, v in self.vars.items() if v.get()]
        if not ids:
            if not messagebox.askyesno(TITLE, '선택한 패치가 없습니다.\n모든 패치를 제거하고 원본으로 되돌릴까요?'):
                return
        else:
            names = '\n'.join(f'{g.id}. {g.label}' for g in self.groups if g.id in ids)
            if not messagebox.askyesno(TITLE, f'다음 패치를 적용합니다. 나머지는 원본으로 되돌립니다.\n\n{names}'):
                return
        self.start(ids)

    def restore(self):
        """원본 복구: 체크를 모두 해제하고 적용한 것과 같다. 끝나면 check()가 체크를 적용 상태(없음)로 맞춘다."""
        if not messagebox.askyesno(TITLE, '모든 패치를 제거하고 원본으로 되돌릴까요?'):
            return
        self.start([])

    def start(self, ids):
        self.busy = True
        self.update_button()
        self.write_log(f'--- {datetime.datetime.now():%Y-%m-%d %H:%M:%S} {"적용: " + ", ".join(map(str, ids)) if ids else "전체 원복"} ---')
        folder = self.folder.get().strip()
        threading.Thread(target=self.worker, args=(folder, ids), daemon=True).start()

    def worker(self, folder, ids):
        log = lambda m: self.logq.put(('log', m))
        try:
            skipped = core.Engine(folder).apply(ids, log=log)
            msg = '원본으로 되돌렸습니다.' if not ids else '패치를 적용했습니다.'
            if skipped and 2 in ids:
                names = '\n'.join(skipped[:8]) + (f'\n외 {len(skipped) - 8}개' if len(skipped) > 8 else '')
                msg += ('\n\n아래 시나리오 파일은 원본과 달라(다른 모드나 편집 도구로 수정된 것으로 보임) 개성 패치를 넣지 않았습니다. '
                        '이 시나리오로 새 게임을 하면 저돌·투장 능력치 추가만 빠지고, 나머지 패치는 정상입니다.\n' + names)
            self.logq.put(('done', msg))
            return
        except PatchError as e:
            err = str(e)
        except PermissionError as e:
            err = (f'게임 파일에 쓰지 못했습니다: {e.filename}\n\n'
                   '게임이나 런처가 켜져 있으면 끄고, 패처를 관리자 권한으로 실행했는지 확인하세요. '
                   '백신 프로그램이 파일을 막았을 수도 있습니다.')
        except Exception as e:
            err = f'예상하지 못한 오류가 발생했습니다: {type(e).__name__}: {e}'
        # 실패하면 원인을 좁힐 수 있게 오류 위치와 환경을 로그에 남긴다(제보는 "로그 복사"로 받는다)
        import traceback
        log('[진단] ' + traceback.format_exc().rstrip())
        for line in diag.collect(folder):
            log('[진단] ' + line)
        self.logq.put(('error', err))

    def drain(self):
        try:
            while True:
                kind, msg = self.logq.get_nowait()
                if kind == 'log':
                    self.write_log(msg)
                    continue
                self.busy = False
                self.write_log(('완료: ' if kind == 'done' else '오류: ') + msg)
                self.check(sync=True)
                if kind == 'done':
                    messagebox.showinfo(TITLE, msg)
                else:
                    self.show_error(msg)
        except queue.Empty:
            pass
        self.root.after(100, self.drain)

    def show_error(self, msg):
        """실패 창: 원인·해결(메시지)을 크게, 제보 안내와 로그 복사 버튼을 함께."""
        win = tk.Toplevel(self.root)
        win.title(TITLE)
        win.transient(self.root)
        win.resizable(False, False)
        frm = ttk.Frame(win, padding=16)
        frm.pack(fill='both', expand=True)
        tk.Label(frm, text='패치를 적용하지 못했습니다', font=(FONT[0], 13, 'bold'), fg=BAD_COLOR).pack(anchor='w')
        tk.Label(frm, text=msg, justify='left', anchor='w', wraplength=560).pack(anchor='w', pady=(10, 0))
        tip = tk.Label(frm, text='제보하실 때는 [로그 복사] 버튼을 눌러 복사한 내용을 함께 전달해 주세요.',
                       justify='left', anchor='w', wraplength=560, bg='#fff8c5', padx=8, pady=6)
        tip.pack(fill='x', pady=(14, 0))
        row = ttk.Frame(frm)
        row.pack(fill='x', pady=(14, 0))
        ttk.Button(row, text='닫기', command=win.destroy).pack(side='right')
        ttk.Button(row, text='로그 복사', command=lambda: self.copy_log(parent=win)).pack(side='right', padx=(0, 6))
        win.bind('<Escape>', lambda e: win.destroy())
        win.grab_set()
        win.focus_set()

    def copy_log(self, parent=None):
        """제보용: 패처·게임 정보와 로그 전체를 클립보드로."""
        head = [TITLE, f'상태: {self.status.cget("text")}']
        try:
            head += diag.collect(self.folder.get().strip())
        except Exception as e:
            head.append(f'진단 실패: {type(e).__name__}: {e}')
        self.root.clipboard_clear()
        self.root.clipboard_append('\n'.join(head) + '\n--- 로그 ---\n' + self.log.get('1.0', 'end'))
        where = f'\n\n로그 파일: {LOG_FILE}' if LOG_FILE else ''
        messagebox.showinfo(TITLE, '진단 정보와 로그를 복사했습니다. 제보할 곳에 붙여 넣으세요.' + where, parent=parent or self.root)

    def write_log(self, msg):
        self.log.config(state='normal')
        self.log.insert('end', msg + '\n')
        self.log.see('end')
        self.log.config(state='disabled')
        _append_file(msg)


def _log_path():
    base = os.environ.get('LOCALAPPDATA')
    if not base:
        return None
    return os.path.join(base, 'san14patch', 'patcher.log')


LOG_FILE = _log_path()  # DLL 로그(dll.log)와 같은 폴더. 창을 닫아도 남는다


def _append_file(msg):
    if not LOG_FILE:
        return
    try:
        os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
        if os.path.exists(LOG_FILE) and os.path.getsize(LOG_FILE) > 2_000_000:
            os.replace(LOG_FILE, LOG_FILE + '.old')
        with open(LOG_FILE, 'a', encoding='utf-8') as f:
            f.write(msg + '\n')
    except OSError:
        pass


def autowrap(label):
    """라벨 글자를 라벨 폭에 맞춰 줄바꿈한다(창을 줄여도 잘리지 않게)."""
    label.bind('<Configure>', lambda e: label.config(wraplength=max(120, e.width - 4)))
    return label


def main():
    if os.name == 'nt':
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)  # 고해상도 화면에서 흐리지 않게
        except Exception:
            pass
    root = tk.Tk()
    app = App(root)
    root.update_idletasks()
    # 처음 크기는 내용에 맞춘 크기, 최소는 그 절반 정도(표·로그는 스크롤하고 글자는 줄바꿈한다)
    root.minsize(*app.min_size())
    root.mainloop()
    return 0


if __name__ == '__main__':
    sys.exit(main())

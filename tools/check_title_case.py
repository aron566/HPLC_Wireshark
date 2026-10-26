#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Title Case 合规性检查 —— 见 docs/CODING_STYLE.md

用法:
    python tools/check_title_case.py                 # 只报「首字母小写」这种硬伤
    python tools/check_title_case.py --strict        # 连带「内部实词小写」全报（含历史遗留）
    python tools/check_title_case.py --list-unit     # 列出被判定为「单位后缀」而跳过的条目

退出码:
    0 = 无硬伤
    1 = 有硬伤（首字母小写）
"""
import os
import re
import sys

# 小写虚词（Title Case 非首词时保持小写）
MINOR = {
    'a', 'an', 'the', 'and', 'or', 'but', 'nor', 'for', 'of', 'in', 'on', 'at',
    'to', 'from', 'by', 'with', 'as', 'than', 'that', 'this', 'these', 'those',
    'per', 'via', 'vs', 'not', 'does',
}

REGISTER_EN = re.compile(
    r'register_en\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)'
)

# 协议术语白名单：驼峰式缩写按协议原样保留，不参与 Title Case 判定。
# eFC = 扩展帧控制（extended Frame Control），协议原文写法，与 PB/MPDU 同级。
PROTO_TERMS = {
    'eFC', 'eCF', 'pBPC', 'nTB',
}

# 前置空格但是 UI 标签（不是单位后缀），需要 Title Case
UI_LABEL_WHITELIST = {'  Display filter:', '  Goto frame:'}


def is_unit_suffix(en: str) -> bool:
    """英文串以空格开头 ⇒ 单位/说明后缀，小写正确，跳过。"""
    return en.startswith(' ')


def is_debug_format(en: str) -> bool:
    """含 ≥2 个 '=' 且含 '%' 的键值对格式串 → 调试输出，跳过。"""
    return en.count('=') >= 2 and '%' in en


def _is_noise_token(token: str) -> bool:
    """扩展名（.bin/.txt）、文件通配(*.zip)、含分号的串 → 不参与 Title Case 判定。"""
    if not token:
        return True
    if token.startswith('.'):                 # .bin / .txt
        return True
    if token.startswith('*'):                 # *.zip / *.*
        return True
    if '.' in token and not token.endswith('.'):
        # 含点但不是句末（File Replay (.bin) / (*.bin);;All）
        return True
    if ';' in token or '\\' in token or '/' in token:
        return True
    return False


def internal_lower_words(en: str):
    """返回首字母大写但内部实词小写的词列表。"""
    if not en or is_unit_suffix(en) or is_debug_format(en):
        return []
    if en.isupper():
        return []
    bad = []
    for i, token in enumerate(en.split(' ')):
        if _is_noise_token(token):
            continue
        # 去掉首尾非单词字符（括号、逗号、点号、百分号）
        word = re.sub(r'^[^\w]+|[^\w]+$', '', token)
        if not word or len(word) <= 1:
            continue
        if word.isupper():                      # 缩写
            continue
        if word in PROTO_TERMS:                 # 协议术语（驼峰式缩写，如 eFC）
            continue
        if re.search(r'\d', word):              # 含数字型号
            continue
        if word.lower() in MINOR:               # 虚词
            continue
        if '-' in word:                         # 连字符复合词（单独规则）
            continue
        if i == 0:                              # 首词已大写
            continue
        if word[0].islower():
            bad.append(word)
    return bad


def check(en: str):
    """返回 (级别, 说明)。级别: 'ok' / 'hard' / 'soft' / 'skip'"""
    if not en:
        return 'skip', ''

    # 前置空格 → 单位后缀（UI 标签白名单除外）
    if is_unit_suffix(en):
        if en in UI_LABEL_WHITELIST:
            # 这条要走 Title Case 检查
            pass
        else:
            return 'skip', 'unit-suffix'

    if is_debug_format(en):
        return 'skip', 'debug-format'

    if en.isupper():
        return 'skip', 'all-upper'

    if re.match(r'[a-z]', en):
        return 'hard', '首字母小写'

    bad = internal_lower_words(en)
    if bad:
        return 'soft', f'内部实词小写: {", ".join(bad)}'

    return 'ok', ''


def title_case(en: str) -> str:
    """给出建议的 Title Case 写法（供人工确认，不自动改文件）。"""
    if not en or is_unit_suffix(en) or is_debug_format(en):
        return en
    out = []
    for i, token in enumerate(en.split(' ')):
        if _is_noise_token(token):          # 扩展名/通配/路径 原样保留
            out.append(token)
            continue
        m = re.match(r'^([^\w]*)(.*?)([^\w]*)$', token)
        if not m:
            out.append(token)
            continue
        pre, word, post = m.group(1), m.group(2), m.group(3)
        if not word:
            out.append(token)
            continue
        if word.isupper() or re.search(r'\d', word):
            new = word
        elif word.lower() in MINOR and i > 0:
            new = word.lower()
        elif '-' in word:
            new = '-'.join(p[:1].upper() + p[1:] for p in word.split('-') if p)
        else:
            new = word[:1].upper() + word[1:]
        out.append(pre + new + post)
    return ' '.join(out)


def main():
    strict = '--strict' in sys.argv
    list_unit = '--list-unit' in sys.argv

    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')
    if not os.path.isdir(root):
        print(f'错误: 找不到 src 目录 ({root})', file=sys.stderr)
        return 2

    hard, soft, skips, total = [], [], [], 0
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if not fn.endswith(('.cpp', '.h')):
                continue
            path = os.path.join(dirpath, fn)
            try:
                src = open(path, encoding='utf-8', errors='replace').read()
            except OSError:
                continue
            for m in REGISTER_EN.finditer(src):
                total += 1
                zh, en = m.group(1), m.group(2)
                line = src[:m.start()].count('\n') + 1
                rel = os.path.relpath(path, os.path.dirname(root)).replace('\\', '/')
                level, why = check(en)
                rec = (rel, line, zh, en, why)
                if level == 'hard':
                    hard.append(rec)
                elif level == 'soft':
                    soft.append(rec)
                elif level == 'skip':
                    skips.append(rec)

    print(f'register_en 调用点总数: {total}')
    print(f'  skip（单位后缀/调试串/纯缩写）: {len(skips)}')
    print(f'  合规:                          {total - len(skips) - len(hard) - len(soft)}')
    print(f'  soft（内部实词小写，历史遗留）:  {len(soft)}')
    print(f'  hard（首字母小写，硬伤）:        {len(hard)}')
    print()

    if hard:
        print('=== 🔴 硬伤（必须修）===')
        for rel, line, zh, en, why in hard:
            print(f'{rel}:{line}')
            print(f'    中文: {zh}')
            print(f'    英文: {en!r}   ← {why}')
            print(f'    建议: {title_case(en)!r}')
        print()

    if strict and soft:
        print('=== ⚠️ 内部实词小写（--strict 模式列出）===')
        for rel, line, zh, en, why in soft:
            print(f'{rel}:{line}  {en!r}  ← {why}')
            print(f'    建议: {title_case(en)!r}')
        print()
    elif soft:
        print(f'（{len(soft)} 条内部实词小写未列出，加 --strict 查看）')
        print()

    if list_unit:
        print('=== 被判定为「单位后缀」而跳过 ===')
        for rel, line, zh, en, why in skips:
            print(f'{rel}:{line}  {en!r}')
        print()

    return 1 if hard else 0


if __name__ == '__main__':
    sys.exit(main())

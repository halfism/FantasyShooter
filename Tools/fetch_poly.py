# -*- coding: utf-8 -*-
"""从 poly.pizza 抓免费 3D 模型（.glb 直链）。

为什么选 poly.pizza：
- 它的模型文件直接暴露在 static.poly.pizza/<uuid>.glb 上，不需要登录、
  不走 Google Drive / itch.io（这两个在本机全 000）。
- 实测下载 ~56KB/s，比 kenney 的 12~16KB/s 快得多，单个模型 70KB 左右。
- 大量模型是"只有 POSITION + NORMAL、无 UV 无贴图"的纯色低模，
  正好匹配工程现在的 Unlit 纯色渲染（我们的贴图管线上不了 GPU）。

授权：poly.pizza 上混有 CC0 与 CC-BY，脚本把每个模型的 license / author
写进 manifest.json，方便以后回查署名。

用法:
    python Tools/fetch_poly.py            # 全量（跳过已存在）
    python Tools/fetch_poly.py --dry      # 只搜索列举，不下载
只依赖标准库。
"""
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

UA = {'User-Agent': ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) '
                     'AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36')}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(ROOT, 'ArtSource', 'Downloaded', 'poly')
LOG = os.path.join(ROOT, 'Saved', 'fetch_poly.log')
MANIFEST = os.path.join(DST, 'manifest.json')

# 俯视视角下只有"立起来、轮廓清晰"的东西能被认出来（横躺的一律读成地面斑块），
# 所以关键词全部指向有高度的物件。
TERMS = [
    'tree', 'pine tree', 'dead tree', 'stump', 'log',
    'rock', 'boulder', 'statue', 'pillar', 'ruins',
    'tombstone', 'cross', 'lantern', 'brazier', 'campfire',
    'well', 'fountain', 'cart', 'banner', 'crate',
    'signpost', 'obelisk', 'crystal', 'mushroom', 'fence',
    'gate', 'altar', 'totem', 'windmill', 'tower',
    'coffin', 'urn', 'anvil', 'tent', 'barrel',
    'bridge', 'arch', 'skull', 'stone wall', 'gravestone',
]
PER_TERM = 5


def log(msg):
    line = '[FSPOLY] %s' % msg
    print(line, flush=True)
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    with open(LOG, 'a', encoding='utf-8') as f:
        f.write(line + '\n')


def get(url, timeout=45, tries=3):
    for i in range(1, tries + 1):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.read()
        except Exception as e:
            if i == tries:
                raise
            log('retry %d/%d %s (%s)' % (i, tries, url, e))
            time.sleep(2 * i)


def search(term, n=PER_TERM):
    q = urllib.parse.quote(term)
    try:
        html = get('https://poly.pizza/search/' + q).decode('utf-8', 'replace')
    except Exception as e:
        log('search failed %s: %s' % (term, e))
        return []
    ids, seen = [], set()
    for mid in re.findall(r'href="/m/([A-Za-z0-9]{8,16})"', html):
        if mid not in seen:
            seen.add(mid)
            ids.append(mid)
        if len(ids) >= n:
            break
    return ids


def page_info(mid):
    """返回 dict 或 None。缺 glb 的直接丢掉。

    元数据优先读页面里的 `window.__SERVER_APP_STATE__` JSON —— 里面 Title /
    Creator.Username / Licence / Tris 都是干净的字段。早期版本用正则抓
    `<h1>`、`>By X<`，作者名一直是 unknown（那个 <a> 是 React 渲染的，前后
    没有纯文本边界），授权也只能得到 "CC BY" 这种缺版本的串。
    """
    try:
        html = get('https://poly.pizza/m/' + mid).decode('utf-8', 'replace')
    except Exception as e:
        log('page failed %s: %s' % (mid, e))
        return None

    glb = re.search(r'https://static\.poly\.pizza/[a-z0-9-]+\.glb', html)
    if not glb:
        return None

    title, lic, author, tris = mid, 'unknown', 'unknown', 0
    m = re.search(r'window\.__SERVER_APP_STATE__\s*=\s*(\{.*?\})\s*</script>', html, re.S)
    if m:
        try:
            st = json.loads(m.group(1))['initialData']['model']
            title = (st.get('Title') or mid).strip()
            lic = (st.get('Licence') or 'unknown').strip()
            author = ((st.get('Creator') or {}).get('Username') or 'unknown').strip()
            tris = int(st.get('Tris') or 0)
        except Exception as e:
            log('state parse failed %s: %s' % (mid, e))
    else:
        m = re.search(r'<h1[^>]*>\s*([^<]{1,60}?)\s*</h1>', html)
        if m:
            title = m.group(1).strip()

    return {'id': mid, 'title': title, 'license': lic, 'author': author,
            'tris': tris, 'glb': glb.group(0)}


def slugify(t):
    s = re.sub(r'[^A-Za-z0-9]+', '_', t).strip('_').lower()
    return s or 'model'


def main():
    dry = '--dry' in sys.argv
    os.makedirs(DST, exist_ok=True)

    old = []
    if os.path.isfile(MANIFEST):
        try:
            old = json.load(open(MANIFEST, encoding='utf-8'))
        except Exception:
            old = []
    known = {r['id']: r for r in old}

    # ---- 0. 只补元数据（不重新下载）：首次跑用的是正则抓法，作者全是 unknown，
    #      授权也缺版本号。这一步按已记录的 id 重抓页面 JSON 并就地更新 manifest。
    if '--meta' in sys.argv:
        rows = []
        for r in old:
            info = page_info(r['id'])
            if info:
                r.update({'title': info['title'], 'license': info['license'],
                          'author': info['author'], 'tris': info['tris'],
                          'glb': info['glb']})
            rows.append(r)
            log('meta %-24s %-12s %-14s tris=%-5d %s' % (
                r.get('name'), r.get('license'), r.get('author'), r.get('tris', 0), r.get('title')))
        with open(MANIFEST, 'w', encoding='utf-8') as f:
            json.dump(rows, f, ensure_ascii=False, indent=1)
        log('meta refresh done n=%d' % len(rows))
        print('FETCH_POLY_EXIT=0')
        return

    # ---- 1. 搜索去重
    cand, seen = [], set()
    for t in TERMS:
        ids = search(t)
        log('search %-14s -> %d' % (t, len(ids)))
        for mid in ids:
            if mid not in seen:
                seen.add(mid)
                cand.append(mid)
    log('candidates=%d' % len(cand))

    if dry:
        log('dry run, stop here')
        return

    # ---- 2. 逐页取元数据 + 下载
    rows, ok, skip, fail = [], 0, 0, 0
    used_names = set(r.get('name') for r in old)
    for i, mid in enumerate(cand, 1):
        prev = known.get(mid)
        if prev and os.path.isfile(os.path.join(DST, prev['name'] + '.glb')):
            rows.append(prev)
            skip += 1
            continue

        info = page_info(mid)
        if not info:
            fail += 1
            continue

        # 文件名用 title 的可读 slug（同一 title 冲突时补 id 尾号）
        base = slugify(info['title'])
        name = base
        if name in used_names:
            name = '%s_%s' % (base, mid[-4:].lower())
        used_names.add(name)

        path = os.path.join(DST, name + '.glb')
        try:
            data = get(info['glb'], timeout=120)
            if len(data) < 200 or data[:4] != b'glTF':
                raise IOError('not gltf (%d bytes)' % len(data))
            with open(path, 'wb') as f:
                f.write(data)
            info['name'] = name
            info['bytes'] = len(data)
            rows.append(info)
            ok += 1
            log('[%d/%d] %-26s %-28s %6dB %s' % (i, len(cand), name, info['license'],
                                                 len(data), info['author']))
        except Exception as e:
            fail += 1
            log('[%d/%d] FAIL %s: %s' % (i, len(cand), mid, e))

    with open(MANIFEST, 'w', encoding='utf-8') as f:
        json.dump(rows, f, ensure_ascii=False, indent=1)

    log('SUMMARY ok=%d skip=%d fail=%d total=%d' % (ok, skip, fail, len(rows)))
    print('FETCH_POLY_EXIT=0')


if __name__ == '__main__':
    main()

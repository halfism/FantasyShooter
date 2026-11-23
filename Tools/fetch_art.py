# -*- coding: utf-8 -*-
"""从 kenney.nl 拉取 CC0 美术包并解压。

用法:
    python Tools/fetch_art.py survival-kit castle-kit ...

设计要点:
- kenney 的下载链接是一次性签名 URL(取完必须立刻下, 且 expires 很短), 所以每次
  现取现用, 失败就整包重来(Range 请求服务端不支持, 无法断点续传)。
- 内网出口带宽实测只有 ~16KB/s, 大包会跑很久, 因此按包逐个下载 + 逐个解压,
  前面的包可以先用起来。
- 只依赖标准库, 不引入 requests。
"""
import json
import os
import sys
import time
import urllib.request
import zipfile

UA = {
    'User-Agent': ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) '
                   'AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36')
}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(ROOT, 'ArtSource', 'Downloaded')
LOG = os.path.join(ROOT, 'Saved', 'fetch_art.log')


def log(msg):
    line = '[FSFETCH] %s' % msg
    print(line, flush=True)
    with open(LOG, 'a', encoding='utf-8') as f:
        f.write(line + '\n')


def get_url(slug):
    hexid = ('assets/' + slug).encode().hex()
    req = urllib.request.Request(
        'https://kenney.nl/download-link/' + hexid,
        headers={**UA, 'Referer': 'https://kenney.nl/assets/' + slug,
                 'X-Requested-With': 'XMLHttpRequest'})
    with urllib.request.urlopen(req, timeout=25) as r:
        return json.load(r)['url']


def fetch(slug, tries=3):
    os.makedirs(DST, exist_ok=True)
    zip_path = os.path.join(DST, slug + '.zip')
    for attempt in range(1, tries + 1):
        try:
            url = get_url(slug)
            t0 = time.time()
            got = 0
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=90) as r:
                total = int(r.headers.get('Content-Length') or 0)
                log('%s start %.2fMB attempt=%d' % (slug, total / 1048576.0, attempt))
                with open(zip_path, 'wb') as f:
                    while True:
                        chunk = r.read(131072)
                        if not chunk:
                            break
                        f.write(chunk)
                        got += len(chunk)
            dt = time.time() - t0
            if total and got < total:
                raise IOError('short read %d/%d' % (got, total))
            with zipfile.ZipFile(zip_path) as z:
                bad = z.testzip()
                if bad:
                    raise IOError('corrupt entry %s' % bad)
            log('%s OK %.2fMB %.0fs (%.0fKB/s)' % (slug, got / 1048576.0, dt, got / 1024.0 / max(dt, 1)))
            return True
        except Exception as e:
            log('%s FAIL attempt=%d %s: %s' % (slug, attempt, type(e).__name__, e))
            time.sleep(3)
    return False


def unpack(slug):
    zip_path = os.path.join(DST, slug + '.zip')
    out = os.path.join(DST, slug)
    if not os.path.exists(zip_path):
        return
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(out)
    counts = {}
    for base, _, files in os.walk(out):
        for fn in files:
            ext = os.path.splitext(fn)[1].lower()
            counts[ext] = counts.get(ext, 0) + 1
    top = sorted(counts.items(), key=lambda kv: -kv[1])
    log('%s unpacked -> %s  %s' % (slug, os.path.relpath(out, ROOT),
                                   ' '.join('%s:%d' % (e or '(noext)', n) for e, n in top[:8])))


def main():
    os.makedirs(os.path.join(ROOT, 'Saved'), exist_ok=True)
    with open(LOG, 'w', encoding='utf-8') as f:
        f.write('')
    slugs = sys.argv[1:]
    ok = []
    for s in slugs:
        if fetch(s):
            unpack(s)
            ok.append(s)
    log('SUMMARY ok=%s failed=%s' % (len(ok), len(slugs) - len(ok)))
    log('FETCH_EXIT=%d' % (0 if len(ok) == len(slugs) else 1))


if __name__ == '__main__':
    main()

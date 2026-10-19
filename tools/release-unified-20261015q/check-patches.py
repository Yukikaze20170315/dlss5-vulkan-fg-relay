"""Apply every merge-kit patch to a fresh copy of its base and compare the result
with the production tree (LF-normalised). Read-only on all inputs; works in a
temporary folder. Prints one line per patch and exits non-zero on any mismatch."""
import io, os, shutil, subprocess, sys, tarfile, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KIT = ROOT / 'dist' / 'release-unified-20261015q' / 'repo' / 'merge' / 'unified-20261015q'
SRC = ROOT / 'src' / 'unified-20261011'
MFG011 = Path(os.environ.get('MFG011_TARBALL', r'C:\Users\Administrator\AppData\Local\Temp\dlss5-pr-scratch\mfg011.tar.gz'))

def archive(repo, commit, dest, paths):
    data = subprocess.run(['git', '-C', str(repo), 'archive', '--format=tar', commit, *paths], check=True, capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(data)) as t:
        t.extractall(dest, filter='data')

def lf_tree(base):
    for p in base.rglob('*'):
        if p.is_file():
            b = p.read_bytes()
            if b'\0' not in b[:8000]:
                p.write_bytes(b.replace(b'\r\n', b'\n'))

def apply(work, patch):
    r = subprocess.run(['git', '-c', 'core.autocrlf=false', 'apply', '--whitespace=nowarn', str(patch)], cwd=work, capture_output=True, text=True)
    return r.returncode, r.stderr.strip()

def compare(work, mapping):
    bad = []
    for rel, prod in mapping.items():
        got = (work / rel).read_bytes() if (work / rel).exists() else None
        want = prod.read_bytes().replace(b'\r\n', b'\n')
        if got != want:
            bad.append(rel)
    return bad

def main():
    failures = 0
    tmp = Path(tempfile.mkdtemp(prefix='check-patches-'))
    try:
        cases = []
        for label, commit in (('vk-fgrelay-20261005', '835c5bf'), ('NIGos-main-d1cc508', 'd1cc508')):
            w = tmp / ('bridge-' + commit); w.mkdir()
            archive(ROOT / 'worktrees' / 'dlss5-bridge-20261005', commit, w, ['src', 'tests'])
            cases.append((w, KIT / 'bridge' / f'bridge-since-{label}.patch', 'bridge'))
        for label, commit in (('rc10-stages1-c3dc63c', 'c3dc63c'), ('v8.5.0-rc10-9bb6c0f', '9bb6c0f')):
            w = tmp / ('generic-' + commit); w.mkdir()
            archive(ROOT / 'worktrees' / 'peqhub-stages1', commit, w, ['.'])
            cases.append((w, KIT / 'generic' / f'generic-since-{label}.patch', 'generic'))
        w = tmp / 'mfg'; w.mkdir(); x = tmp / 'mfgx'; x.mkdir()
        with tarfile.open(MFG011) as t:
            t.extractall(x, filter='data')
        shutil.copytree(next(x.iterdir()) / 'src' / 'addons' / 'mfgunlock', w / 'addon')
        cases.append((w, KIT / 'mfg' / 'mfg-since-MFGAmpereUnlock-0.11.patch', 'mfg'))
        for work, patch, kind in cases:
            lf_tree(work)
            code, err = apply(work, patch)
            if code:
                print(f'FAIL apply {patch.name}: {err[:400]}'); failures += 1; continue
            if kind == 'bridge':
                mapping = {('src/' + p.relative_to(SRC / 'bridge' / 'src').as_posix()): p for p in (SRC / 'bridge' / 'src').rglob('*')
                           if p.is_file() and p.suffix.lower() not in {'.obj', '.lib', '.exp', '.res', '.addon64'}}
                mapping.update({('tests/' + p.relative_to(SRC / 'bridge' / 'tests').as_posix()): p for p in (SRC / 'bridge' / 'tests').rglob('*') if p.is_file()})
            elif kind == 'generic':
                g = SRC / 'generic' / 'src' / 'addons' / 'dlss5'
                mapping = {p.relative_to(g).as_posix(): p for p in g.rglob('*') if p.is_file()
                           and not p.relative_to(g).as_posix().startswith(('contrib/', '.git/'))}
            else:
                mapping = {('addon/' + p.name): p for p in (SRC / 'mfg' / 'addon').iterdir() if p.is_file()}
                mapping['tests/native-policy.cpp'] = SRC / 'mfg' / 'tests' / 'native-policy.cpp'
                mapping['version.rc'] = SRC / 'mfg' / 'version.rc'
            bad = compare(work, mapping)
            print(f"{'OK  ' if not bad else 'FAIL'} {patch.name}: {len(mapping)} production files compared, {len(bad)} differ {bad[:5]}")
            failures += bool(bad)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    sys.exit(1 if failures else 0)

if __name__ == '__main__':
    main()

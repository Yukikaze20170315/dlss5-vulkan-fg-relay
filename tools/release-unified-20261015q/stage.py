"""Stage the unified-20261015q repository update and release assets.

Writes only below dist/release-unified-20261015q/. Text written by hand (README,
release notes, merge and evidence READMEs) lives in that folder too and is not
generated here; this script produces the merge kit, the source and evidence
archives, and the hash lists. Run from anywhere:

    python tools/release-unified-20261015q/stage.py

Inputs (read only): src/unified-20261011, tools/unified-20261011,
worktrees/dlss5-bridge-20261005 (git), worktrees/peqhub-stages1 (git), the
MFG Unlock 0.11 tag tarball, dist/unified-20261015q-candidate(.zip), and the
evidence/builds folders listed in EVIDENCE below.
"""
import hashlib, io, json, os, re, shutil, subprocess, sys, tarfile, tempfile, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'dist' / 'release-unified-20261015q'
REPO = OUT / 'repo'
ASSETS = OUT / 'assets'
KIT = REPO / 'merge' / 'unified-20261015q'
SRC = ROOT / 'src' / 'unified-20261011'
MFG011 = Path(os.environ.get('MFG011_TARBALL', r'C:\Users\Administrator\AppData\Local\Temp\dlss5-pr-scratch\mfg011.tar.gz'))
BRIDGE_GIT = ROOT / 'worktrees' / 'dlss5-bridge-20261005'
GENERIC_GIT = ROOT / 'worktrees' / 'peqhub-stages1'
FG_RELAY_GIT = ROOT / 'worktrees' / 'dlss5-vulkan-fg-relay'

BINARY_SUFFIXES = {'.obj', '.lib', '.exp', '.res', '.addon64', '.dll', '.exe', '.pdb', '.ilk', '.iobj', '.ipdb', '.cso', '.spv', '.jpg', '.png'}

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()

def git_tree(repo, commit, dest, paths):
    data = subprocess.run(['git', '-C', str(repo), 'archive', '--format=tar', commit, *paths], check=True, capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        tar.extractall(dest)

def text_files(base):
    out = {}
    for p in sorted(base.rglob('*')):
        if p.is_file() and p.suffix.lower() not in BINARY_SUFFIXES and '.git' not in p.relative_to(base).parts:
            out[p.relative_to(base).as_posix()] = p
    return out

def lf(data):
    return data.replace(b'\r\n', b'\n')

def make_patch(old_files, new_files, name, allow_delete):
    """Unified diff a/<path> b/<path>, both sides normalised to LF."""
    work = Path(tempfile.mkdtemp(prefix='stage-patch-'))
    try:
        (work / 'a').mkdir(); (work / 'b').mkdir()
        keys = set(new_files) | (set(old_files) if allow_delete else set())
        changed, added, deleted = [], [], []
        for k in sorted(keys):
            o = lf(old_files[k].read_bytes()) if k in old_files else None
            n = lf(new_files[k].read_bytes()) if k in new_files else None
            if o == n:
                continue
            if o is not None:
                (work / 'a' / k).parent.mkdir(parents=True, exist_ok=True); (work / 'a' / k).write_bytes(o)
            if n is not None:
                (work / 'b' / k).parent.mkdir(parents=True, exist_ok=True); (work / 'b' / k).write_bytes(n)
            (changed if o is not None and n is not None else added if n is not None else deleted).append(k)
        r = subprocess.run(['git', '-c', 'core.autocrlf=false', '-c', 'core.quotepath=false', 'diff', '--no-index', '--no-prefix', '--no-renames', '--full-index', 'a', 'b'],
                           cwd=work, capture_output=True)
        if r.returncode not in (0, 1):
            raise RuntimeError(r.stderr.decode('utf-8', 'replace'))
        target = KIT / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(r.stdout)
        return {'patch': name, 'modified': changed, 'added': added, 'deleted': deleted}
    finally:
        shutil.rmtree(work, ignore_errors=True)

def copy_changed(files, rels, dest):
    for k in rels:
        t = dest / k
        t.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(files[k], t)

def stage_merge_kit():
    report = {}
    tmp = Path(tempfile.mkdtemp(prefix='stage-bases-'))
    try:
        # Bridge: production tree bridge/src and bridge/tests against the published bases.
        new_bridge = {('src/' + k): v for k, v in text_files(SRC / 'bridge' / 'src').items()}
        new_bridge.update({('tests/' + k): v for k, v in text_files(SRC / 'bridge' / 'tests').items()})
        for label, commit in (('vk-fgrelay-20261005', '835c5bf'), ('NIGos-main-d1cc508', 'd1cc508')):
            base = tmp / ('bridge-' + commit)
            git_tree(BRIDGE_GIT, commit, base, ['src', 'tests'])
            old = text_files(base)
            # The production tree carries only the tests it changed; files it does not
            # carry are unchanged, not deleted. src/ is complete, so deletions there count.
            deletions_ok = {k: v for k, v in old.items() if k.startswith('src/')}
            rep = make_patch(old, new_bridge, f'bridge/bridge-since-{label}.patch', allow_delete=False)
            rep['deleted'] = sorted(k for k in deletions_ok if k not in new_bridge)
            report['bridge-' + label] = rep
        rep = report['bridge-vk-fgrelay-20261005']
        copy_changed(new_bridge, rep['modified'] + rep['added'], KIT / 'bridge' / 'changed-files')

        # Generic: the dlss5 add-on folder (without the bundled contrib/ copy of earlier
        # release material) against rc10-stages1 and against v8.5.0-rc10.
        gsrc = SRC / 'generic' / 'src' / 'addons' / 'dlss5'
        new_generic = {k: v for k, v in text_files(gsrc).items() if not k.startswith('contrib/')}
        for label, commit in (('rc10-stages1-c3dc63c', 'c3dc63c'), ('v8.5.0-rc10-9bb6c0f', '9bb6c0f')):
            base = tmp / ('generic-' + commit)
            git_tree(GENERIC_GIT, commit, base, ['.'])
            old = text_files(base)
            report['generic-' + label] = make_patch(old, new_generic, f'generic/generic-since-{label}.patch', allow_delete=True)
        rep = report['generic-rc10-stages1-c3dc63c']
        copy_changed(new_generic, rep['modified'] + rep['added'], KIT / 'generic' / 'changed-files')

        # MFG Unlock: addon/ against the official 0.11 tag; tests and version resource are new.
        base = tmp / 'mfg011'
        base.mkdir()
        with tarfile.open(MFG011) as tar:
            tar.extractall(base)
        top = next(base.iterdir())
        old = {('addon/' + k): v for k, v in text_files(top / 'src' / 'addons' / 'mfgunlock').items()}
        new_mfg = {('addon/' + k): v for k, v in text_files(SRC / 'mfg' / 'addon').items()}
        new_mfg['tests/native-policy.cpp'] = SRC / 'mfg' / 'tests' / 'native-policy.cpp'
        new_mfg['version.rc'] = SRC / 'mfg' / 'version.rc'
        report['mfg-0.11'] = make_patch(old, new_mfg, 'mfg/mfg-since-MFGAmpereUnlock-0.11.patch', allow_delete=True)
        rep = report['mfg-0.11']
        copy_changed(new_mfg, rep['modified'] + rep['added'], KIT / 'mfg' / 'changed-files')

        # Shared headers: all new.
        for p in sorted((SRC / 'common').iterdir()):
            t = KIT / 'common' / p.name
            t.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, t)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    (KIT / 'kit-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report

# Build outputs left in bridge/src by its own build.cmd, the Generic folder's git
# metadata, and the contrib/ copy of earlier release material (already published
# in the repository) are left out; third-party import libraries under deps/ and
# upstream/ are kept because the build links them. The NGX SDK headers under
# mfg/deps/ngx carry NVIDIA's proprietary licence notice and are not redistributed.
SOURCE_EXCLUDE = re.compile(r'(^|/)(contrib/dlss5-bridge-vulkan-fg-relay/|\.git/)|^src/unified-20261011/mfg/deps/ngx/|^src/unified-20261011/bridge/src/[^/]+\.(obj|lib|exp|res|addon64)$|\.(pdb|ilk|iobj|ipdb)$', re.I)

def stage_source_zip():
    target = ASSETS / 'unified-20261015q-source.zip'
    count = 0
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for base, prefix in ((SRC, 'src/unified-20261011'), (ROOT / 'tools' / 'unified-20261011', 'tools/unified-20261011'),
                             (ROOT / 'upstream' / 'NVIDIA-nvapi-87dca62', 'upstream/NVIDIA-nvapi-87dca62')):
            for p in sorted(base.rglob('*')):
                if not p.is_file():
                    continue
                rel = prefix + '/' + p.relative_to(base).as_posix()
                if SOURCE_EXCLUDE.search(rel):
                    continue
                z.write(p, rel); count += 1
        z.write(OUT / 'source-BUILDING.md', 'BUILDING.md'); count += 1
    return count

EVIDENCE = [
    'evidence/unified-20261011/run-20261016-0006-1q-z-fullmode',
    'evidence/unified-20261011/b-20261016-1q-framegraph',
    'evidence/unified-20261011/friend-20261014-mfg-not-effective',
    'evidence/unified-20261011/run-20261012-0125-fix2-verify',
    'evidence/unified-20261011/run-20261012-1538-1o-launch-fail',
    'evidence/unified-20261011/root-cause-20261011',
    'evidence/unified-20261011/run-20261011-205058-458533',
    'evidence/unified-20261011/run-20261011-151829-054468',
    'evidence/unified-20261011/layers-20261011-132704-299349',
    'evidence/unified-20261011/layers-20261011-133539-634381',
    'evidence/unified-20261011/run-20261011-122003-635237',
    'evidence/unified-20261011/run-20261011-122232-037323',
    'evidence/unified-20261011/startup-20261011-132843-821',
    'evidence/unified-20261011/startup-20261011-133243-332',
    'evidence/unified-20261011/run-20261011-133733-295355',
    'evidence/unified-20261011/clean-after-repair-20261011',
    'evidence/mfg-blackwell-codex-20261011/bw3-six-x-20261011-105429-465',
    'evidence/throttle-ab-20261011',
    'evidence/pacing-baseline-20261011-185902-a3721a08',
    'evidence/pacing-baseline-20261011-190254-407eea86',
    'evidence/pacing-baseline-20261011-190447-d7fe0833',
    'evidence/pacing-baseline-20261011-190831-8bbd4a02',
    'evidence/pacing-baseline-20261011-164510-9d1aaacd',
    'evidence/pacing-baseline-20261011-164719-c4483cd0',
    'evidence/pacing-baseline-20261011-165132-9cd46b84',
    'evidence/pacing-baseline-20261011-165350-9b36c42e',
    'evidence/pacing-baseline-20261011-165938-c2e60bf1',
    'evidence/pacing-baseline-20261011-174209-c177cf6b',
    'evidence/machines-20261010/README.md',
    'builds/unified-20261011/package-1q/package-verification.json',
    'builds/unified-20261011/package-1q-recovery-fix',
    'builds/unified-20261011/cmd-flow-29871203a5ee445fbed4c321477a872e',
    'builds/unified-20261011/mfg-unload-pin1p/race-fixture',
    'builds/unified-20261011/continuation1f',
    'builds/unified-20261011/recovery',
    'builds/unified-20261011/hook-ui-1g',
    'builds/unified-20261011/ngx-path-1h',
    'builds/unified-20261011/unified1q-generic-build.log',
    'builds/unified-20261011/pq-wait-fix2-build.log',
]
# Binaries are not evidence. On PC B, collect-env-output.txt also lists that
# user's installed software and browser extensions, and b-pull-*.zip duplicates raw/.
EVIDENCE_SKIP = re.compile(r'\.(exe|dll|addon64|obj|lib|pdb|etl)$|b-20261016-1q-framegraph/(collect-env-output\.txt|b-pull-[^/]*\.zip)$', re.I)

def stage_evidence_zip():
    target = ASSETS / 'unified-20261015q-evidence.zip'
    rows = []
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.write(REPO / 'docs' / 'unified-20261015q' / 'EVIDENCE.md', 'README.md')
        for item in EVIDENCE:
            src = ROOT / item
            files = [src] if src.is_file() else sorted(p for p in src.rglob('*') if p.is_file())
            for p in files:
                rel = p.relative_to(ROOT).as_posix()
                if EVIDENCE_SKIP.search(rel):
                    continue
                z.write(p, rel); rows.append((sha256(p), rel))
        raw = OUT / 'evidence-raw'
        for p in sorted(raw.rglob('*')):
            if p.is_file():
                rel = 'offline-rerun/' + p.relative_to(raw).as_posix()
                z.write(p, rel); rows.append((sha256(p), rel))
        listing = ''.join(f'{h}  {r}\n' for h, r in rows)
        z.writestr('SHA256SUMS.txt', listing)
    return len(rows)

def stage_repo():
    """Byte copies of already-published material; nothing here is generated."""
    count = 0
    readme = subprocess.run(['git', '-C', str(FG_RELAY_GIT), 'show', '248cfe5:README.md'],
                            check=True, capture_output=True).stdout
    target = REPO / 'docs' / 'history' / 'README-vk-fgrelay-20261005.md'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(readme); count += 1
    usage = ROOT / 'dist' / 'unified-20261015q-candidate' / 'DLSS5-使用与恢复 Usage and Recovery.md'
    shutil.copy2(usage, REPO / 'docs' / 'unified-20261015q' / 'USAGE-AND-RECOVERY.md'); count += 1
    tools_out = REPO / 'tools' / 'release-unified-20261015q'
    tools_out.mkdir(parents=True, exist_ok=True)
    for name in ('stage.py', 'check-patches.py', 'rerun-offline-tests.ps1'):
        shutil.copy2(ROOT / 'tools' / 'release-unified-20261015q' / name, tools_out / name); count += 1
    return count

def write_sums():
    rows = []
    for p in sorted(ASSETS.iterdir()):
        if p.is_file() and p.name != 'SHA256SUMS.txt':
            rows.append(f'{sha256(p)}  {p.name}\n')
    (ASSETS / 'SHA256SUMS.txt').write_text(''.join(rows), encoding='utf-8', newline='\n')
    rows = []
    for p in sorted(REPO.rglob('*')):
        if p.is_file():
            rows.append(f'{sha256(p)}  {p.relative_to(REPO).as_posix()}\n')
    (OUT / 'REPO-MANIFEST.txt').write_text(''.join(rows), encoding='utf-8', newline='\n')

if __name__ == '__main__':
    step = sys.argv[1] if len(sys.argv) > 1 else 'all'
    ASSETS.mkdir(parents=True, exist_ok=True)
    if step in ('kit', 'all'):
        # The merge README is hand-written; the kit rebuild keeps its bytes.
        kit_readme = KIT / 'README.md'
        kept = kit_readme.read_bytes() if kit_readme.is_file() else None
        if KIT.exists():
            shutil.rmtree(KIT)
        if kept is not None:
            kit_readme.parent.mkdir(parents=True, exist_ok=True)
            kit_readme.write_bytes(kept)
        print(json.dumps({k: {x: len(v) if isinstance(v, list) else v for x, v in r.items()} for k, r in stage_merge_kit().items()}, indent=1))
    if step in ('repo', 'all'):
        print('repo copies', stage_repo())
    if step in ('assets', 'all'):
        print('source files', stage_source_zip())
        print('evidence files', stage_evidence_zip())
        shutil.copy2(ROOT / 'dist' / 'unified-20261015q-candidate.zip', ASSETS / 'unified-20261015q-candidate.zip')
    if step in ('sums', 'assets', 'all'):
        write_sums()
        print((ASSETS / 'SHA256SUMS.txt').read_text(encoding='utf-8'))

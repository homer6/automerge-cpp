#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the software bill of materials: sbom.spdx.json (SPDX 2.3) and sbom.cdx.json (CycloneDX).

    python3 tools/sbom.py                    # write both files at the repository root
    python3 tools/sbom.py --version v1.2.3   # name the release (cut-release passes it)
    python3 tools/sbom.py --check            # exit 1 if the committed files are out of date

What goes in:
- every package syft (https://github.com/anchore/syft) finds in the files git tracks: lock files
  and manifests for npm, Python, Go, Rust and the rest. Paths listed in `.public-exclude`, and
  hosted CI under `.github/workflows/`, are left out, as they are left out of the public tree;
- the C and C++ components syft cannot see (git submodules, CMake FetchContent downloads, vendored
  code and fonts), read from the list `tools/third-party-licenses.py` (or its
  `tools/third-party-licenses.json`) keeps for THIRD_PARTY_LICENSES, each with its name, version
  or commit, license and download URL;
- every git submodule, even one that list misses, at its pinned commit;
- the base image of every Dockerfile (`FROM`), as written there, and EXTRA_COMPONENTS below.

Reproducible: it scans the tree of one commit (the last that changed anything but the SBOM files
and this script), not the working directory, so builds, caches and installed dependencies never
leak in. The document's creation time is that commit's date, its namespace is named after that
commit, the CycloneDX serial number is derived from the namespace, and no tool version is written.
Run it twice on the same commit and the files are byte for byte the same (with the same syft).

Needs: git, Python 3.10+, and syft on PATH (`brew install syft`, or
https://github.com/anchore/syft#installation). Tested with syft SYFT_TESTED below; another
version may list packages differently, so regenerate with that one.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import uuid

# --- this repository --------------------------------------------------------------------------
REPOSITORY = "automerge-cpp"
LICENSE = "MIT"                                   # SPDX expression of this repository
COPYRIGHT = "Copyright (c) 2026 Steve Sperandeo"
SUPPLIER = "Organization: OramaSearch, Inc."      # who distributes it (and creates this SBOM)
ORIGINATOR = "Person: Steve Sperandeo"                             # a fork names its upstream here
HOME = "https://github.com/homer6/automerge-cpp"
# Components neither a lock file nor tools/third-party-licenses.* lists (a base model, for example),
# in that list's form: name, version, license, use, url.
EXTRA_COMPONENTS: list[dict] = []
# --- end of the repository's settings ---------------------------------------------------------

SYFT_TESTED = "1.57.1"
OUTPUTS = ("sbom.spdx.json", "sbom.cdx.json")
SELF = "tools/sbom.py"
ALWAYS_EXCLUDED = ["**/.github/workflows/"]
# Repositories whose public home is not github.com/oramasearch/<name>.
PUBLIC_HOME = {"oramasearch/automerge-cpp": "homer6/automerge-cpp"}
# Lets syft read only what is in the tree: no network, no local caches.
SYFT_ENV = {
    "SYFT_CHECK_FOR_APP_UPDATE": "false",
    "SYFT_GOLANG_SEARCH_LOCAL_MOD_CACHE_LICENSES": "false",
    "SYFT_GOLANG_SEARCH_LOCAL_VENDOR_LICENSES": "false",
    "SYFT_GOLANG_SEARCH_REMOTE_LICENSES": "false",
    "SYFT_JAVASCRIPT_SEARCH_REMOTE_LICENSES": "false",
    "SYFT_JAVA_USE_NETWORK": "false",
    "SYFT_JAVA_USE_MAVEN_LOCAL_REPOSITORY": "false",
    "SYFT_PYTHON_SEARCH_REMOTE_LICENSES": "false",
    "SYFT_DOTNET_SEARCH_REMOTE_LICENSES": "false",
    "SYFT_ENRICH": "",
}

ROOT = subprocess.run(["git", "rev-parse", "--show-toplevel"], check=True, capture_output=True,
                      text=True).stdout.strip()


def git(*args: str, binary: bool = False, check: bool = True, stdin=None):
    r = subprocess.run(["git", "-C", ROOT, *args], capture_output=True, input=stdin, check=False)
    if check and r.returncode != 0:
        raise SystemExit(f"git {' '.join(args)}: {r.stderr.decode(errors='replace').strip()}")
    return r.stdout if binary else r.stdout.decode("utf-8", "surrogateescape")


def source_commit(rev: str) -> str:
    """The last commit at or before rev that changed anything but the SBOM and this script."""
    excludes = [f":(exclude){p}" for p in (*OUTPUTS, SELF)]
    sha = git("log", "-1", "--format=%H", rev, "--", ".", *excludes).strip()
    return sha or git("rev-parse", rev).strip()


def public_url(url: str) -> str:
    """A repository URL as the public reads it: https, and the repository's public home."""
    url = re.sub(r"^(?:ssh://)?git@github\.com[:/]", "https://github.com/", url.strip())
    url = re.sub(r"\.git/?$", "", url)
    for private, public in PUBLIC_HOME.items():
        url = re.sub(rf"github\.com/{re.escape(private)}\b", f"github.com/{public}", url)
    return url


def excluded_paths(commit: str, paths: list[str]) -> set[str]:
    """Paths matched by .public-exclude (gitignore syntax) or ALWAYS_EXCLUDED."""
    spec = git("show", f"{commit}:.public-exclude", check=False) + "\n" + "\n".join(ALWAYS_EXCLUDED)
    with tempfile.TemporaryDirectory() as scratch:
        subprocess.run(["git", "init", "-q", scratch], check=True)
        patterns = os.path.join(scratch, "patterns")
        with open(patterns, "w", encoding="utf-8") as fh:
            fh.write(spec + "\n.public-exclude\n")
        r = subprocess.run(["git", "-C", scratch, "-c", f"core.excludesFile={patterns}", "check-ignore",
                            "--no-index", "--stdin", "-z"], input="\0".join(paths).encode(),
                           capture_output=True)
    return {p for p in r.stdout.decode("utf-8", "surrogateescape").split("\0") if p}


def tree(commit: str) -> list[tuple[str, str, str]]:
    """(type, object, path) of every entry of the commit's tree."""
    out = []
    for entry in git("ls-tree", "-r", "-z", "--full-tree", commit).split("\0"):
        if entry:
            meta, path = entry.split("\t", 1)
            _, kind, obj = meta.split()
            out.append((kind, obj, path))
    return out


def extract(commit: str, skip: set[str], into: str) -> None:
    data = git("archive", "--format=tar", commit, binary=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        members = [m for m in tar.getmembers() if m.name.rstrip("/") not in skip and not m.issym()
                   and not m.islnk()]
        tar.extractall(into, members=members, filter="data")


def run_syft(directory: str, version: str, out: str) -> dict:
    if not shutil.which("syft"):
        raise SystemExit("syft is not installed: brew install syft (or see the syft README)")
    found = subprocess.run(["syft", "version"], capture_output=True, text=True).stdout
    m = re.search(r"Version:\s*(\S+)", found)
    if m and m.group(1) != SYFT_TESTED:
        print(f"note: syft {m.group(1)}, tested with {SYFT_TESTED}; the output may differ", file=sys.stderr)
    env = {**os.environ, **SYFT_ENV}
    subprocess.run(["syft", "scan", f"dir:{directory}", "-q", "--source-name", REPOSITORY,
                    "--source-version", version, "-o", f"spdx-json={out}"], check=True, env=env)
    with open(out, encoding="utf-8") as fh:
        return json.load(fh)


# --- the C and C++ components -----------------------------------------------------------------

def listed_components(commit: str) -> list[dict]:
    """The components tools/third-party-licenses.{py,json} lists, as of the commit."""
    spec = git("show", f"{commit}:tools/third-party-licenses.json", check=False)
    if spec.strip():
        return json.loads(spec)["components"]
    code = git("show", f"{commit}:tools/third-party-licenses.py", check=False)
    if "COMPONENTS" not in code:
        return []
    with tempfile.TemporaryDirectory() as scratch:
        path = os.path.join(scratch, "third_party_licenses.py")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(code)
        loader = importlib.util.spec_from_file_location("third_party_licenses", path)
        module = importlib.util.module_from_spec(loader)
        loader.loader.exec_module(module)
    return list(getattr(module, "COMPONENTS", []))


def license_expression(text: str, refs: dict) -> str:
    """An SPDX license expression for a free-text license, or a LicenseRef carrying the text."""
    if not text or text in ("-", "NOASSERTION"):
        return "NOASSERTION"
    expr = re.sub(r"\s+\(.*\)\s*$", "", text.strip())          # "BSD-3-Clause (used under BSD)"
    expr = re.sub(r"(?<=[A-Za-z0-9.+])/(?=[A-Za-z])", " OR ", expr)  # "MIT/Apache-2.0"
    tokens = re.findall(r"\(|\)|[^\s()]+", expr)
    ident = re.compile(r"^(LicenseRef-[A-Za-z0-9.-]+|[A-Za-z0-9][A-Za-z0-9.+-]*)$")
    if tokens and all(t in ("(", ")", "AND", "OR", "WITH") or (ident.match(t) and any(c.isdigit() or c == "-"
                      for c in t) or t in ("MIT", "ISC", "Zlib", "Unlicense", "BSL-1.0", "curl", "libpng",
                                           "JSON", "Beerware", "PostgreSQL", "Python-2.0", "blessing")) for t in tokens):
        return expr
    ref = "LicenseRef-" + re.sub(r"[^A-Za-z0-9.-]+", "-", text).strip("-")[:60]
    refs[ref] = text
    return ref


def first_url(*values) -> str | None:
    for v in values:
        m = re.search(r"https?://\S+", str(v or ""))
        if m:
            return m.group(0).rstrip(").,")
    return None


def spdx_id(prefix: str, *parts: str) -> str:
    digest = hashlib.sha256("\0".join(parts).encode()).hexdigest()[:16]
    name = re.sub(r"[^A-Za-z0-9.-]+", "-", parts[0]).strip("-")[:40]
    return f"SPDXRef-{prefix}-{name}-{digest}"


def component_packages(commit: str, entries, refs: dict) -> list[dict]:
    pins = {path: obj for kind, obj, path in entries if kind == "commit"}
    modules = {}
    gm = git("show", f"{commit}:.gitmodules", check=False)
    current = None
    for line in gm.splitlines():
        s = line.strip()
        if s.startswith("path"):
            current = s.split("=", 1)[1].strip()
        elif s.startswith("url") and current:
            modules[current] = s.split("=", 1)[1].strip()
    out, seen_paths = [], set()
    for c in listed_components(commit) + EXTRA_COMPONENTS:
        sub = c.get("submodule")
        if sub:
            seen_paths.add(sub)
            version = pins.get(sub, "NOASSERTION")
            url = public_url(modules.get(sub) or first_url(c.get("url"), c.get("source")) or "")
            location = f"git+{url}.git@{version}" if url and version != "NOASSERTION" else "NOASSERTION"
        else:
            version = str(c.get("version") or "NOASSERTION")
            version = "NOASSERTION" if version.strip() in ("-", "") else version
            url = first_url(c.get("url"), c.get("source"))
            if not url and c.get("github"):
                url = f"https://github.com/{c['github']}"
            url = public_url(url) if url else None
            location = url or "NOASSERTION"
        name, _, aside = c["name"].partition(" (")
        pkg = {"name": name, "SPDXID": spdx_id("Component", name, version),
               "versionInfo": version, "downloadLocation": location, "filesAnalyzed": False,
               "supplier": "NOASSERTION", "copyrightText": "NOASSERTION",
               "licenseConcluded": "NOASSERTION",
               "licenseDeclared": license_expression(str(c.get("license", "")), refs),
               "comment": (f"{aside.rstrip(')')}. " if aside else "")
                          + f"How it is used: {c.get('use', 'not stated')}. Listed in THIRD_PARTY_LICENSES."}
        if url:
            pkg["homepage"] = url
            m = re.match(r"https://github\.com/([^/]+)/([^/]+)$", url)
            if m and version not in ("NOASSERTION", "-"):
                pkg["externalRefs"] = [{"referenceCategory": "PACKAGE-MANAGER", "referenceType": "purl",
                                        "referenceLocator": f"pkg:github/{m.group(1)}/{m.group(2)}@{version}"}]
        out.append(pkg)
    for path, version in sorted(pins.items()):
        if path in seen_paths:
            continue
        url = public_url(modules.get(path, ""))
        name = url.rsplit("/", 1)[-1] if url else path
        pkg = {"name": name, "SPDXID": spdx_id("Submodule", name, version), "versionInfo": version,
               "downloadLocation": f"git+{url}.git@{version}" if url else "NOASSERTION",
               "filesAnalyzed": False, "supplier": "NOASSERTION", "copyrightText": "NOASSERTION",
               "licenseConcluded": "NOASSERTION", "licenseDeclared": "NOASSERTION",
               "comment": f"git submodule at {path}."}
        if url:
            pkg["homepage"] = url
        out.append(pkg)
    return out


def base_images(commit: str, entries, skip: set[str]) -> list[dict]:
    """A package for every image a tracked Dockerfile builds FROM (not stages, not scratch)."""
    found: dict[str, set[str]] = {}
    for kind, _, path in entries:
        base = path.rsplit("/", 1)[-1]
        if kind != "blob" or path in skip or not re.search(r"(^|\.)(Dockerfile|Containerfile)(\.|$)", base):
            continue
        args, stages = {}, set()
        for line in git("show", f"{commit}:{path}", check=False).splitlines():
            m = re.match(r"\s*ARG\s+([A-Za-z_][A-Za-z0-9_]*)=(\S+)", line)
            if m:
                args[m.group(1)] = m.group(2).strip("\"'")
            m = re.match(r"\s*FROM\s+(?:--platform=\S+\s+)?(\S+)(?:\s+AS\s+(\S+))?", line, re.I)
            if not m:
                continue
            image = re.sub(r"\$\{?([A-Za-z_][A-Za-z0-9_]*)(?::-([^}]*))?\}?",
                           lambda v: args.get(v.group(1), v.group(2) or "$" + v.group(1)), m.group(1))
            if m.group(2):
                stages.add(m.group(2).lower())
            if image.lower() in stages or image == "scratch" or "$" in image:
                continue
            found.setdefault(image, set()).add(path)
    out = []
    for image, paths in sorted(found.items()):
        ref, _, digest = image.partition("@")
        name, _, tag = ref.rpartition(":") if ":" in ref.rsplit("/", 1)[-1] else (ref, "", "")
        if not (digest or tag):
            continue  # a build context or a locally built stage, not a published image
        version = f"{tag}@{digest}" if tag and digest else digest or tag
        out.append({"name": name, "SPDXID": spdx_id("Image", name, version), "versionInfo": version,
                    "downloadLocation": "NOASSERTION", "filesAnalyzed": False, "supplier": "NOASSERTION",
                    "copyrightText": "NOASSERTION", "licenseConcluded": "NOASSERTION",
                    "licenseDeclared": "NOASSERTION", "primaryPackagePurpose": "CONTAINER",
                    "comment": f"Container base image ({image}), FROM in " + ", ".join(sorted(paths))
                               + ". Its own packages are in the image SBOM of each release image.",
                    "externalRefs": [{"referenceCategory": "PACKAGE-MANAGER", "referenceType": "purl",
                                      "referenceLocator": f"pkg:docker/{name}@{(digest or tag).replace(':', '%3A')}"
                                                          + (f"?tag={tag}" if tag and digest else "")}]})
    return out


def plain(name: str) -> str:
    return re.sub(r"^(crate|npm|package|pypi|go)\s+", "", name.lower()).strip()


# --- the documents ----------------------------------------------------------------------------

def build(rev: str, version: str | None) -> tuple[str, str]:
    commit = source_commit(rev)
    created = subprocess.run(["git", "-C", ROOT, "show", "-s", "--format=%cd",
                              "--date=format-local:%Y-%m-%dT%H:%M:%SZ", commit],
                             capture_output=True, text=True, check=True,
                             env={**os.environ, "TZ": "UTC"}).stdout.strip()
    version = version or commit[:12]
    namespace = f"{HOME}/sbom/{commit}"
    entries = tree(commit)
    skip = excluded_paths(commit, [p for kind, _, p in entries if kind == "blob"])
    with tempfile.TemporaryDirectory() as scratch:
        src = os.path.join(scratch, REPOSITORY)
        os.mkdir(src)
        extract(commit, skip, src)
        doc = run_syft(src, version, os.path.join(scratch, "syft.spdx.json"))
        text = json.dumps(doc)
        for local in (src, os.path.realpath(src), scratch, os.path.realpath(scratch)):
            text = text.replace(local + "/", "/").replace(local, "/")
        doc = json.loads(text)

    refs: dict[str, str] = {}
    described = set(doc.get("documentDescribes", []))
    described |= {r["relatedSpdxElement"] for r in doc.get("relationships", [])
                  if r["spdxElementId"] == "SPDXRef-DOCUMENT" and r["relationshipType"] == "DESCRIBES"}
    old_root = next(iter(described)) if described else None
    root = "SPDXRef-DocumentRoot"
    packages = [p for p in doc.get("packages", []) if p["SPDXID"] != old_root]
    packages.append({
        "name": REPOSITORY, "SPDXID": root, "versionInfo": version,
        "downloadLocation": f"git+{HOME}.git", "homepage": HOME, "filesAnalyzed": False,
        "supplier": SUPPLIER, "originator": ORIGINATOR, "licenseConcluded": LICENSE,
        "licenseDeclared": LICENSE, "copyrightText": COPYRIGHT,
        "primaryPackagePurpose": "SOURCE",
        "comment": f"The source tree of {REPOSITORY}. Packages found by syft in the files git tracks, "
                   "plus the C and C++ components of THIRD_PARTY_LICENSES and the git submodules."})
    have = {(plain(p["name"]), p.get("versionInfo", "")) for p in packages}
    rels = []
    for r in doc.get("relationships", []):
        if r["spdxElementId"] == "SPDXRef-DOCUMENT" and r["relationshipType"] == "DESCRIBES":
            continue
        r = dict(r)
        if "SPDXRef-File-" in r["spdxElementId"] + r["relatedSpdxElement"]:
            continue  # the SBOM lists packages, not the manifest files they were read from
        if r["spdxElementId"] == old_root:
            r["spdxElementId"] = root
        if r["relatedSpdxElement"] == old_root:
            r["relatedSpdxElement"] = root
        rels.append(r)
    for pkg in component_packages(commit, entries, refs) + base_images(commit, entries, skip):
        if (plain(pkg["name"]), pkg["versionInfo"]) in have:
            continue
        have.add((plain(pkg["name"]), pkg["versionInfo"]))
        packages.append(pkg)
        rels.append({"spdxElementId": root, "relatedSpdxElement": pkg["SPDXID"],
                     "relationshipType": "DEPENDS_ON"})
    rels.append({"spdxElementId": "SPDXRef-DOCUMENT", "relatedSpdxElement": root,
                 "relationshipType": "DESCRIBES"})
    extracted = {e["licenseId"]: e for e in doc.get("hasExtractedLicensingInfos", [])}
    for ref, text in refs.items():
        extracted.setdefault(ref, {"licenseId": ref, "name": text,
                                   "extractedText": f"{text}: the full text is in THIRD_PARTY_LICENSES."})

    out = {
        "spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0", "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"{REPOSITORY}-{version}", "documentNamespace": namespace,
        "creationInfo": {"created": created, "creators": [SUPPLIER, "Tool: tools/sbom.py", "Tool: syft"],
                         "licenseListVersion": doc.get("creationInfo", {}).get("licenseListVersion", "3.27"),
                         "comment": f"Made from source commit {commit} by tools/sbom.py."},
        "documentDescribes": [root],
        "packages": sorted(packages, key=lambda p: (p["name"].lower(), p.get("versionInfo", ""), p["SPDXID"])),
        "hasExtractedLicensingInfos": sorted(extracted.values(), key=lambda e: e["licenseId"]),
        "relationships": sorted({json.dumps(r, sort_keys=True) for r in rels}),
    }
    out["relationships"] = [json.loads(r) for r in out["relationships"]]
    if not out["hasExtractedLicensingInfos"]:
        del out["hasExtractedLicensingInfos"]
    spdx = json.dumps(out, indent=2, sort_keys=False, ensure_ascii=False) + "\n"
    spdx = re.sub(r"git@github\.com:", "https://github.com/", spdx)
    return spdx, cyclonedx(spdx, namespace, created)


def cyclonedx(spdx: str, namespace: str, created: str) -> str:
    with tempfile.TemporaryDirectory() as scratch:
        src, dst = os.path.join(scratch, "in.spdx.json"), os.path.join(scratch, "out.cdx.json")
        with open(src, "w", encoding="utf-8") as fh:
            fh.write(spdx)
        subprocess.run(["syft", "convert", src, "-q", "-o", f"cyclonedx-json={dst}"], check=True,
                       env={**os.environ, **SYFT_ENV})
        with open(dst, encoding="utf-8") as fh:
            bom = json.load(fh)
    bom["serialNumber"] = f"urn:uuid:{uuid.uuid5(uuid.NAMESPACE_URL, namespace)}"
    meta = bom.setdefault("metadata", {})
    meta["timestamp"] = created
    meta["tools"] = {"components": [{"type": "application", "name": "tools/sbom.py"},
                                    {"type": "application", "name": "syft"}]}
    meta["supplier"] = {"name": SUPPLIER.split(": ", 1)[1]}
    if "component" in meta:
        meta["component"]["supplier"] = {"name": SUPPLIER.split(": ", 1)[1]}
    bom["components"] = sorted(bom.get("components", []),
                               key=lambda c: (c.get("name", "").lower(), c.get("version", ""), c.get("bom-ref", "")))
    if "dependencies" in bom:
        bom["dependencies"] = sorted(({**d, "dependsOn": sorted(d.get("dependsOn", []))} for d in bom["dependencies"]),
                                     key=lambda d: d["ref"])
    return json.dumps(bom, indent=2, ensure_ascii=False) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rev", default="HEAD", help="the commit to describe (default HEAD)")
    ap.add_argument("--version", help="the release version (default: the source commit, 12 digits)")
    ap.add_argument("--check", action="store_true", help="exit 1 if the committed files are out of date")
    a = ap.parse_args()
    files = dict(zip(OUTPUTS, build(a.rev, a.version)))
    for local in ("/Users/", "/home/", "/private/tmp", "/var/folders"):
        if any(local in text for text in files.values()):
            raise SystemExit(f"refusing to write: a local path ({local}) reached the SBOM")
    if a.check:
        stale = [n for n, text in files.items()
                 if not os.path.exists(os.path.join(ROOT, n)) or open(os.path.join(ROOT, n), encoding="utf-8").read() != text]
        if stale:
            print("out of date: " + ", ".join(stale) + "; run python3 tools/sbom.py")
            return 1
        return 0
    for name, text in files.items():
        with open(os.path.join(ROOT, name), "w", encoding="utf-8") as fh:
            fh.write(text)
    n = len(json.loads(files["sbom.spdx.json"])["packages"]) - 1
    print(f"sbom.spdx.json, sbom.cdx.json: {n} components")
    return 0


if __name__ == "__main__":
    sys.exit(main())

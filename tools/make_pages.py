#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 David Ryan
"""RELAY-16's site (GitHub Pages), from release packages already built (../build.sh --release VERSION) and the browser
emulator (base/tools/build.py app --web --release VERSION):

  fm1-base's make_site.py for the FM-1: index.html (app/site/landing.md), install/, manual/ (app/docs/MANUAL.md),
  emu/, words/, firmware/
  then an installer page per SMK-37 model, the same page with that model's package: install-smk37/,
  install-smk37-elite/, install-smk37-mke/ (make_site.py takes only identities that start with id_prefix, FM-1_...).
  The SMK-37 pages offer no m-bootloader (fm1-base ships the FM-1's) and send people back to the stock firmware by
  M-VAVE's own updater (the page's stock section installs the FM-1's).

  tools/make_pages.py VERSION OUT_DIR"""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "base" / "tools"))
import make_site as ms  # noqa: E402

SMK = (("", "SMK-37 Pro", "the SMK-37 Pro"), ("-elite", "SMK-37 Elite", "the SMK-37 Elite"),
       ("-mke", "MKE-P37", "the MKE-P37"))


def main(version, out):
    app, out = ROOT / "app", Path(out).resolve()
    conf = ms.read_conf(app / "fm1.toml")
    ms.main(app, app / "build" / f"relay-16-{version}.fwsc", version, out)
    template = (ms.SRC / "web" / "installer.html").read_text(encoding="utf-8")
    lib = "\n".join(ms.strip_module((ms.SRC / "web" / f).read_text(encoding="utf-8"))
                    for f in ("fm1ports.js", "fm1pkg.js", "fm1ota.js"))
    repo, credits = conf.get("repo", ""), conf.get("credits", "")
    for suffix, model, words in SMK:
        pkg = app / "build" / f"relay-16-smk37{suffix}-{version}.fwsc"
        raw = pkg.read_bytes()
        product = ms.product_of(raw)
        if not product.startswith(model + "_") or b"FELUCCA-LOADER-1" not in raw:
            raise SystemExit(f"{pkg}: identity {product!r}: not a release build for the {model}")
        shutil.copy(pkg, out / "firmware" / pkg.name)
        meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + pkg.name,
                           "backup": False, "res": None, "boot": None})
        html = template
        for key, val in (("RES", "RELAY-16's data"), ("RES_JA", "RELAY-16 のデータ"), ("NAME", conf["name"]), ("REPO", repo),
                         ("REPO_SHORT", repo.replace("https://", "")), ("LICENSE", conf.get("licence", "MPL-2.0")),
                         ("CREDITS", " " + credits if credits else "")):
            html = html.replace("{{" + key + "}}", val)
        html = html.replace("<h1>Install RELAY-16</h1>", f"<h1>Install RELAY-16 on {words}</h1>")
        html = html.replace("<title>Install · RELAY-16</title>", f"<title>Install · RELAY-16 · {model}</title>")
        start = html.index('<h2 data-t="stockh">')
        end = html.index('<h2 data-t="reporth">')
        note = (f'<h2>Back to the stock firmware</h2>\n  <p class="small">Install M-VAVE\'s firmware for {words} with '
                "M-VAVE's own updater, M-UPGRADE. The installer packages for the stock firmware stay with M-VAVE."
                "</p>\n  ")
        html = html[:start] + note + '<div hidden>' + html[start:end] + '</div>\n  ' + html[end:]
        # the page's words name the unit: the FM-1 there is this model (not in identities, FM-1_..., the stock
        # package's file name, or the parser's own model string)
        html = re.sub(r'(?<!model: ")FM-1(?![_\w.])', model, html)
        html = html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*APP*/", "null")
        d = out / f"install-smk37{suffix}"
        d.mkdir(parents=True, exist_ok=True)
        (d / "index.html").write_text(html, encoding="utf-8")
        print(f"pages: {d.name}/ ({product}), firmware/{pkg.name} "
              f"sha256 {hashlib.sha256(raw).hexdigest()[:16]}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])

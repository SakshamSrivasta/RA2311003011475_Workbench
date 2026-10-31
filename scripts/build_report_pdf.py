"""Renders docs/REPORT.md to docs/REPORT.pdf (A4) with headless Chrome.
Documentation tooling only. Needs: python3 -m pip install --user markdown==3.7
Usage: python3 scripts/build_report_pdf.py   (set CHROME=... if not on macOS)"""
import os, pathlib, subprocess, tempfile

import markdown

ROOT = pathlib.Path(__file__).resolve().parent.parent
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
CSS = """@page { size: A4; margin: 16mm 15mm; }
body { font: 10pt/1.42 "Helvetica Neue", Arial, sans-serif; color: #111; }
h1 { font-size: 16pt; margin: 0 0 4pt; } h2 { font-size: 12pt; margin: 12pt 0 4pt; border-bottom: 1px solid #ccc; padding-bottom: 2pt; }
p, li { margin: 3pt 0; } ul { margin: 2pt 0; padding-left: 16pt; }
table { border-collapse: collapse; margin: 5pt 0; font-size: 9pt; } th, td { border: 1px solid #bbb; padding: 2pt 6pt; text-align: left; }
th { background: #f0f0f0; } code { font-size: 8.8pt; background: #f4f4f4; padding: 0 2px; } img { page-break-inside: avoid; }"""

md = (ROOT / "docs/REPORT.md").read_text(encoding="utf-8")
md = md.replace('src="../results/', f'src="{(ROOT / "results").as_uri()}/')
body = markdown.markdown(md, extensions=["tables", "md_in_html"])
with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8") as f:
    f.write(f'<!doctype html><html><head><meta charset="utf-8"><title>Workbench report</title><style>{CSS}</style></head><body>{body}</body></html>')
subprocess.run([CHROME, "--headless=new", "--allow-file-access-from-files", "--no-pdf-header-footer",
                f"--print-to-pdf={ROOT / 'docs/REPORT.pdf'}", pathlib.Path(f.name).as_uri()],
               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
os.unlink(f.name)
print("wrote docs/REPORT.pdf")

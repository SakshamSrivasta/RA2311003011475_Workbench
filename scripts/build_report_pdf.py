"""Renders a Markdown report to an A4 PDF with headless Chrome.
Documentation tooling only. Needs: python3 -m pip install --user markdown==3.7
Usage: python3 scripts/build_report_pdf.py [report.md] [report.pdf]
       (defaults: docs/REPORT.md -> docs/REPORT.pdf; set CHROME=... if not on macOS)
Relative image paths are resolved against the Markdown file's folder."""
import os, pathlib, re, subprocess, sys, tempfile

import markdown

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "docs/REPORT.md"
OUT = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else SRC.with_suffix(".pdf")
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
CSS = """@page { size: A4; margin: 14mm 14mm 15mm; }
body { font: 9.6pt/1.4 "Helvetica Neue", Arial, sans-serif; color: #111; }
h1 { font-size: 16pt; margin: 0 0 3pt; } h2 { font-size: 12pt; margin: 11pt 0 4pt; border-bottom: 1px solid #ccc; padding-bottom: 2pt; }
h3 { font-size: 10.2pt; margin: 8pt 0 3pt; }
p, li { margin: 3pt 0; } ul, ol { margin: 2pt 0; padding-left: 16pt; }
table { border-collapse: collapse; margin: 5pt 0; font-size: 8.6pt; } th, td { border: 1px solid #bbb; padding: 1.5pt 5pt; text-align: left; vertical-align: top; }
th { background: #f0f0f0; } code { font-size: 8.6pt; background: #f4f4f4; padding: 0 2px; }
img { max-width: 100%; page-break-inside: avoid; }
figure { margin: 6pt 0; page-break-inside: avoid; } figcaption { font-size: 8.4pt; color: #444; margin-top: 2pt; }
.row { display: flex; gap: 8pt; align-items: flex-start; } .row > figure { flex: 1; margin: 4pt 0; }
.small { font-size: 8.6pt; } .pb { page-break-before: always; }
.key { border-left: 3px solid #2a78d6; padding: 3pt 8pt; background: #f5f8fc; margin: 6pt 0; }"""

md = SRC.read_text(encoding="utf-8")


def absolutise(match):
    attr, path = match.group(1), match.group(2)
    if re.match(r"^[a-z]+:", path):
        return match.group(0)
    return f'{attr}="{(SRC.parent / path).resolve().as_uri()}"'


body = markdown.markdown(md, extensions=["tables", "md_in_html", "fenced_code"])
body = re.sub(r'(src)="([^"]+)"', absolutise, body)
with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8") as f:
    f.write(f'<!doctype html><html><head><meta charset="utf-8"><title>{SRC.stem}</title><style>{CSS}</style></head><body>{body}</body></html>')
subprocess.run([CHROME, "--headless=new", "--allow-file-access-from-files", "--no-pdf-header-footer",
                f"--print-to-pdf={OUT}", pathlib.Path(f.name).as_uri()],
               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
os.unlink(f.name)
print("wrote", OUT)

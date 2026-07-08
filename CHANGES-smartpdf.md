# smartpdf changes

This fork of SumatraPDF contains one focused change on top of the official code, plus a
small Python tool (`tools-smartpdf/`) that prepares schematic PDFs for it.

## What was changed (in plain words)

Our schematic PDFs get processed by `tools-smartpdf/smartify.py`, which makes three kinds
of text clickable. The forked reader understands the special links the tool writes:

1. **Net names** (e.g. `CLK`, `VREG_S4A_1P8`) carry a hidden `search:NAME?z=200` link.
   Clicking one does the same as Ctrl+F: the Find box fills with the name, the view jumps
   to the next place it appears, highlights it — and zooms in (the `z=200` part; the tool's
   `--zoom` option controls it, `0` turns it off). F3 / Shift+F3 move between matches.
   "Whole word" and "Match case" are forced on, so clicking `CLK` never lands on `CLK_EN`.
2. **Manufacturer part numbers** (e.g. `DRV2604YZFR`) carry an ordinary web link that
   opens the browser with a Google "…datasheet" search. This part works in ANY PDF viewer.
3. **Company part numbers** (`yyy-xxxx`, e.g. `214-0034`) carry a hidden `folder:` link.
   Clicking one opens File Explorer at the company parts library
   (`\\datastore\groups\Engineering\Approved Component\yyy\yyy-xxxx\Data Sheets`).
   If the exact folder doesn't exist, the reader walks up to the nearest parent that does.

The tool also adds a **bookmarks sidebar** (the PDF outline panel): a Nets tree and a Parts
tree, each entry jumping to that name's first occurrence — works in any viewer.

The `search:` and `folder:` links are handled entirely inside the reader and are never
passed to Windows — no protocol handlers, no registry entries. Every other kind of link
(web addresses, page jumps, table of contents) behaves exactly like the official reader.

## Where the changes live

- File: `src/MainWindow.cpp`, function `LinkHandler::LaunchURL` — the single door every
  clicked link walks through, for every document type. Two `if` blocks marked with
  `smartpdf` comments (about 55 lines total): one for `search:` (+optional zoom), one for
  `folder:`.
- Tool: `tools-smartpdf/smartify.py` (adds the links to a PDF),
  `tools-smartpdf/make_test_pdf.py` + `smartpdf-test.pdf` (test sheet covering every
  behavior — instructions printed on page 1).
- CI: `.github/workflows/smartpdf-build.yml` builds `SumatraPDF.exe` on GitHub's Windows
  servers on every push to the smartpdf branch; download it from the run's Artifacts.

## Versions

- **v0.1.0** — commit `789a6f5`: click-to-search for nets (no zoom), smartify v1, CI, test PDF.
- **v0.2.0** — zoom on net click, browser search for manufacturer part numbers, File
  Explorer for company part numbers, bookmarks sidebar, smarter token classification.

(Tags exist in the local development clone; pushing tags is blocked in the build
environment, so use the commit hashes above to check out a version.)

## How to carry this change onto a newer SumatraPDF release

The viewer change is small and self-contained, so updating is easy:

1. Fetch the new official release tag from the upstream repository
   (https://github.com/sumatrapdfreader/sumatrapdf).
2. Rebase or cherry-pick this fork's commits onto the new tag, e.g.
   `git rebase <new-tag>` on this branch.
3. If `LaunchURL` moved or was renamed, search the code for `SumatraLaunchBrowser` —
   the `search:`/`folder:` checks belong immediately before the code path that reaches it.
4. Rebuild on Windows (Visual Studio 2022, Release | x64, `vs2022\SumatraPDF.sln`,
   project `SumatraPDF`) — or just push and let the CI workflow build it.

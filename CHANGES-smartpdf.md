# smartpdf changes

This fork of SumatraPDF contains exactly **one** change on top of the official code.

## What was changed (in plain words)

Our schematic PDFs contain clickable net labels. Each label hides a special link that reads
`search:NET_NAME` (for example `search:CLK`).

In the official reader, clicking such a link would be handed to Windows, which doesn't know
what `search:` means. In this fork, the reader catches those links itself and instead does
the same thing as pressing Ctrl+F and typing the net name:

- the Find box fills in with the net name,
- the view jumps to the next place that name appears and highlights it,
- F3 / Shift+F3 then move to the next / previous match as usual.

"Whole word" and "Match case" are switched on automatically, so clicking `CLK` never lands
on `CLK_EN` or `CLK_OUT`.

The `search:` link is never passed to Windows — no protocol handlers, no registry entries.
Every other kind of link (web addresses, page jumps, table of contents) behaves exactly like
the official reader.

## Where the change lives

- File: `src/MainWindow.cpp`
- Function: `LinkHandler::LaunchURL`
- Size: one added `if` block of about 25 lines, marked with `smartpdf` comments.

That function is the single door every clicked link walks through, for every document type,
which is why the change is made there and nowhere else.

## How to carry this change onto a newer SumatraPDF release

The change is tiny and self-contained, so updating is easy:

1. Fetch the new official release tag from the upstream repository
   (https://github.com/sumatrapdfreader/sumatrapdf).
2. Rebase or cherry-pick this fork's single commit onto the new tag, e.g.
   `git rebase <new-tag>` on this branch.
3. If `LaunchURL` moved or was renamed, search the code for `SumatraLaunchBrowser` —
   the `search:` check belongs immediately before the code path that reaches it.
4. Rebuild on Windows (Visual Studio 2022, Release | x64, `vs2022\SumatraPDF.sln`,
   project `SumatraPDF`).

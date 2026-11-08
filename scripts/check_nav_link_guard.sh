#!/usr/bin/env bash
# Fails if the guard that keeps heading/contents-page links from becoming endnotes is removed.
f="$(dirname "$0")/../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp"
ok=1
for needle in "isContentsPageHref(href)" "noLinkDepth < 0" "containsWordStartNoCase(navType, \"toc\")"; do
  grep -qF "$needle" "$f" || { echo "MISSING guard: $needle"; ok=0; }
done
[ "$ok" = 1 ] && echo "nav link guard OK" || { echo "FAIL: heading/contents links could be treated as endnotes again"; exit 1; }

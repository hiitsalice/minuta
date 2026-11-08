#!/usr/bin/env python3
"""Builds test/epubs/test_toc_links.epub: chapter headings and contents pages with links
(must stay plain text) plus one real endnote (must still work)."""
import zipfile, sys, os

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'test', 'epubs', 'test_toc_links.epub')

NS = 'xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"'
def page(body):
    return f'<?xml version="1.0" encoding="utf-8"?>\n<html {NS}><head><title>t</title></head><body>{body}</body></html>'

toc = page('<div class="contents" id="TOC"><h2>Contents</h2>'
           '<p><a href="ch1.html#c1">1.</a> First Chapter</p>'
           '<p><a href="ch2.html#c2">2.</a> Second Chapter</p>'
           '<p><a href="ch1.html#c1">Part One:</a> The Facts</p></div>')
ch1 = page('<div id="c1"><h2><a href="toc.html#chapter_01">One</a></h2><h2>First Chapter</h2>'
           '<p>Plain text with a real note.<a epub:type="noteref" id="ref-1" href="notes.html#note-1"><sup>1</sup></a> More text.</p></div>')
ch2 = page('<div id="c2"><h2><a href="toc.html#chapter_02">Two</a></h2><h2>Second Chapter</h2>'
           '<p>Nothing special here.</p></div>')
notes = page('<ol><li id="note-1" epub:type="endnote"><p>The real endnote. <a epub:type="backlink" href="ch1.html#ref-1">↩</a></p></li></ol>')

opf = '''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>TOC links test</dc:title><dc:language>en</dc:language><dc:identifier id="id">toc-links-test</dc:identifier></metadata>
<manifest><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
<item id="toc" href="toc.html" media-type="application/xhtml+xml"/>
<item id="ch1" href="ch1.html" media-type="application/xhtml+xml"/>
<item id="ch2" href="ch2.html" media-type="application/xhtml+xml"/>
<item id="notes" href="notes.html" media-type="application/xhtml+xml"/></manifest>
<spine toc="ncx"><itemref idref="toc"/><itemref idref="ch1"/><itemref idref="ch2"/><itemref idref="notes"/></spine>
</package>'''
ncx = '''<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>TOC links test</text></docTitle><navMap>
<navPoint id="n1" playOrder="1"><navLabel><text>Contents</text></navLabel><content src="toc.html"/></navPoint>
<navPoint id="n2" playOrder="2"><navLabel><text>First Chapter</text></navLabel><content src="ch1.html"/></navPoint>
<navPoint id="n3" playOrder="3"><navLabel><text>Second Chapter</text></navLabel><content src="ch2.html"/></navPoint>
<navPoint id="n4" playOrder="4"><navLabel><text>Notes</text></navLabel><content src="notes.html"/></navPoint>
</navMap></ncx>'''
container = '''<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'''

with zipfile.ZipFile(out, 'w') as z:
    z.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
    z.writestr('META-INF/container.xml', container, compress_type=zipfile.ZIP_DEFLATED)
    for name, data in [('content.opf', opf), ('toc.ncx', ncx), ('toc.html', toc), ('ch1.html', ch1), ('ch2.html', ch2), ('notes.html', notes)]:
        z.writestr('OEBPS/' + name, data, compress_type=zipfile.ZIP_DEFLATED)
print('wrote', os.path.abspath(out))

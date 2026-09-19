#!/usr/bin/env python3
"""One-way, audited PPTX -> Hype trial conversion. Never modifies source decks.

This is a development migration tool, not a general PowerPoint importer.
It preserves native text, extracts artwork, and records unsupported compositions.
"""
import argparse
import copy
from trial_io import publish_directories
import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile

NS = {'p': 'http://schemas.openxmlformats.org/presentationml/2006/main',
      'a': 'http://schemas.openxmlformats.org/drawingml/2006/main',
      'r': 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'}
R = '{' + NS['r'] + '}'

def relationships(z, part):
    path = str(PurePosixPath(part).parent / '_rels' / (PurePosixPath(part).name + '.rels'))
    if path not in z.NameToInfo: return {}
    return {e.get('Id'): (e.get('Target') if e.get('TargetMode') == 'External' else
             posixpath.normpath(str(PurePosixPath(part).parent / e.get('Target'))).lstrip('/'))
            for e in ET.fromstring(z.read(path))}

def text_of(shape):
    paragraphs = []
    for p in shape.findall('.//a:p', NS):
        text = ''.join('\n' if e.tag == '{'+NS['a']+'}br' else (e.text or '')
                       for e in p.iter() if e.tag in ('{'+NS['a']+'}t', '{'+NS['a']+'}br'))
        paragraphs.extend(text.split('\n'))
    return '\n'.join(paragraphs).strip()

def box(shape):
    transform = shape.find('./p:spPr/a:xfrm', NS)
    if transform is None: return (0, 0, 0, 0)
    off, size = transform.find('a:off', NS), transform.find('a:ext', NS)
    if off is None or size is None: return (0, 0, 0, 0)
    return tuple(int(v) for v in (off.get('x'), off.get('y'), size.get('cx'), size.get('cy')))

def escape(text):
    text = re.sub(r'([\\`*_\[\]<>|])', r'\\\1', text).replace('\r', '')
    return re.sub(r'(?m)^( {0,3})(#{1,6}(?= )|[-+](?= )|[0-9]+[.)](?= ))',
                  lambda m: m[1] + re.sub(r'([#.+)\-])', r'\\\1', m[2]), text)

def markdown_text(texts, year, number):
    values = [t['text'] for t in texts]
    if not values: return ''
    if len(values) == 2 and all('\n' in v for v in values):
        rows = [v.splitlines() for v in values]
        if abs(texts[0]['box'][0] - texts[-1]['box'][0]) > 1000000:
            output = '| ' + ' | '.join(escape(r[0]) for r in rows) + ' |\n| :--- | :--- |\n'
            for i in range(1, max(map(len, rows))):
                output += '| ' + ' | '.join(escape(r[i]) if i < len(r) else '' for r in rows) + ' |\n'
            return output
    text = '\n'.join(values)
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if text.startswith(('“', '"')) or (len(text) > 220 and any('—' in v for v in values)):
        quote = []; credit = []
        for line in lines:
            (credit if line.startswith(('—', 'Alan Kay')) else quote).append(line)
        return '> ' + ' '.join(escape(v) for v in quote).strip('“”"') + ('\n\n'+'\n'.join(escape(v) for v in credit) if credit else '')
    if year == '2024' and number == 42:
        return '```sh\n' + text + '\n```'
    if any('→' in v or '\U000f0155' in v for v in lines) or any(re.search(r' {3,}', v) for v in text.splitlines()):
        return '```text\n' + text + '\n```'
    if len(lines) == 1:
        if len(text) < 100: return '# ' + escape(text)
        return escape(text)
    if len(lines) == 2 and (lines[1].startswith('(') or len(values) == 2):
        return '# ' + escape(lines[0]) + '\n\n' + escape(lines[1])
    if len(lines) <= 4 and len(text) < 100:
        return '\\\n'.join(escape(line) for line in lines)
    return '\n'.join('- ' + escape(line) for line in lines)

def convert(args):
    output = Path(args.output).resolve()
    if (output/'presentation.md').exists() and not args.overwrite:
        raise SystemExit('Trial already exists; choose another directory or --overwrite.')
    (output/'images').mkdir(parents=True, exist_ok=True)
    (output/'videos').mkdir(exist_ok=True)
    entries, slides = [], []
    with zipfile.ZipFile(args.source) as z:
        presentation = ET.fromstring(z.read('ppt/presentation.xml'))
        size = presentation.find('p:sldSz', NS)
        sw, sh = int(size.get('cx')), int(size.get('cy'))
        rels = relationships(z, 'ppt/presentation.xml')
        parts = [rels[e.get(R+'id')] for e in presentation.findall('p:sldIdLst/p:sldId', NS)]
        for number, part in enumerate(parts, 1):
            root = ET.fromstring(z.read(part)); rel = relationships(z, part)
            notes, texts, pictures = [], [], []
            for shape in root.findall('.//p:sp', NS):
                value = text_of(shape)
                if value: texts.append({'text': value, 'box': box(shape)})
            # Native text stays readable/themeable; ignore original font and colors.
            texts.sort(key=lambda t: (round(t['box'][1]/200000), t['box'][0]))
            seen = set()
            texts = [t for t in texts if not ((t['text'], t['box']) in seen or seen.add((t['text'], t['box'])))]
            for pic in root.findall('.//p:pic', NS):
                blip = pic.find('.//a:blip', NS)
                if blip is None: continue
                target = rel.get(blip.get(R+'embed'))
                if not target or target not in z.NameToInfo:
                    notes.append('External or missing picture'); continue
                data = z.read(target); digest = hashlib.sha256(data).hexdigest()[:12]
                extension = PurePosixPath(target).suffix.lower()
                name = 'asset-' + digest + extension
                dest = output/'images'/name
                if not dest.exists(): dest.write_bytes(data)
                crop = pic.find('./p:blipFill/a:srcRect', NS)
                geometry = box(pic)
                pictures.append({'name': name, 'path': dest, 'box': geometry,
                                 'crop': dict(crop.attrib) if crop is not None else {}})
            body = markdown_text(texts, args.year, number)
            kind = 'native-text' if body else 'blank'
            if pictures:
                picture = pictures[0]
                if len(pictures) == 1:
                    name = picture['name']
                    if picture['crop'] and any(int(v) for v in picture['crop'].values()):
                        name = f'slide-{number:03}-crop.png'
                        dims = subprocess.check_output(['magick', 'identify', '-format', '%w %h', str(picture['path'])], text=True).split()
                        iw, ih = map(int,dims); c = picture['crop']
                        left,top,right,bottom = [int(c.get(k,0))/100000 for k in ('l','t','r','b')]
                        x,y = round(iw*left),round(ih*top); w,h = max(1,round(iw*(1-left-right))),max(1,round(ih*(1-top-bottom)))
                        subprocess.run(['magick',str(picture['path']),'-crop',f'{w}x{h}{x:+d}{y:+d}','+repage',str(output/'images'/name)],check=True)
                    x,y,w,h = picture['box']; full = w/sw > .9 and h/sh > .9
                    directive = 'span' if full else 'fit'
                    # A complex meme is represented as text + existing artwork, not falsely claimed pixel-equivalent.
                    if body:
                        directive = 'fit'; kind='native-text-and-artwork'
                        notes.append('Text and artwork reflowed; original absolute positioning is not retained')
                    else: kind='artwork'
                    body = (body+'\n\n' if body else '') + f'![{directive}]({name})'
                else:
                    name=f'slide-{number:03}-artwork.png'
                    scale = min(1920 / sw, 1080 / sh)
                    cw, ch = round(sw * scale), round(sh * scale)
                    command=['magick','-size',f'{cw}x{ch}','canvas:none']
                    for pic in pictures:
                        x,y,w,h=pic['box'];w=max(1,round(w*scale));h=max(1,round(h*scale));x=round(x*scale);y=round(y*scale)
                        command += ['(',str(pic['path'])]
                        crop = pic['crop']
                        if any(int(v) for v in crop.values()):
                            iw, ih = map(int, subprocess.check_output(['magick', 'identify', '-format', '%w %h', str(pic['path'])], text=True).split())
                            l, t, r, b = [int(crop.get(k, 0)) / 100000 for k in ('l', 't', 'r', 'b')]
                            command += ['-crop', f'{max(1, round(iw*(1-l-r)))}x{max(1, round(ih*(1-t-b)))}{round(iw*l):+d}{round(ih*t):+d}', '+repage']
                        command += ['-resize',f'{w}x{h}!','-repage',f'{x:+d}{y:+d}',')']
                    command += ['-layers','merge','+repage',str(output/'images'/name)]
                    subprocess.run(command,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                    body=(body+'\n\n' if body else '')+f'![fit]({name})'
                    kind='composited-artwork-and-text' if texts else 'composited-artwork'
                    notes.append(f'{len(pictures)} picture layers flattened; check crops/group transforms')
            if root.findall('.//p:grpSp', NS):
                notes.append('Grouped shapes: inherited geometry is unsupported; compare with original')
            if any(shape.find('./p:spPr/a:xfrm', NS) is None for shape in root.findall('.//p:sp', NS)):
                notes.append('Layout/master placeholder geometry is unsupported; compare with original')
            if root.findall('.//p:graphicFrame', NS): notes.append('Native chart/table needs visual review')
            if not body:
                body='<!-- Empty or unsupported source slide -->';notes.append('No text or picture found; inspect original')
            slides.append(f'<!-- Source: Rails World {args.year}, slide {number} -->\n\n'+body.strip())
            entries.append({'slide':number,'kind':kind,'text_shapes':len(texts),'pictures':len(pictures),'notes':notes})
    header=f'---\ntitle: Rails World {args.year}\ntheme: {args.theme}\nfont: JetBrains Mono\n---\n\n'
    (output/'presentation.md').write_text(header+'\n\n---\n\n'.join(slides)+'\n')
    report={'source':str(Path(args.source).resolve()),'year':args.year,'slides':entries,
            'limitations':['One-way trial conversion; not a general importer.',
                           'Native text uses Hype theme colors. Pixels inside artwork/screenshots retain their colors.',
                           'Animation, original text coordinates, and decorative shapes are not imported.',
                           'Check all flagged slides against the originals before presenting.']}
    (output/'import-report.json').write_text(json.dumps(report,indent=2)+'\n')
    counts={kind:sum(e['kind']==kind for e in entries) for kind in sorted({e['kind'] for e in entries})}
    print(json.dumps({'year':args.year,'slides':len(slides),'representations':counts,'flagged':sum(bool(e['notes']) for e in entries)}))

def run(args):
    output = Path(args.output).resolve()
    if output.exists() and not args.overwrite:
        raise SystemExit('Trial already exists; choose another directory or --overwrite.')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.hype-import-', dir=output.parent) as staging:
        staged = copy.copy(args)
        staged.output = str(Path(staging) / output.name)
        convert(staged)
        publish_directories(staging, output.parent, [output.name])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source');parser.add_argument('output');parser.add_argument('--year',required=True)
    parser.add_argument('--theme',default='tokyo-night');parser.add_argument('--overwrite',action='store_true')
    run(parser.parse_args())

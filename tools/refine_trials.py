#!/usr/bin/env python3
"""Apply documented, source-backed refinements to the three development trials."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile
from trial_io import publish_directories
import yaml


def objects(path):
    doc = yaml.safe_load(path.read_text())
    return {a['header']['identifier']: o for c in doc['chunks'] for a in c['archives'] for o in a['objects']}


def mappings(value):
    if isinstance(value, dict):
        if 'fileName' in value and 'identifier' in value:
            yield value['identifier'], value
        for item in value.values(): yield from mappings(item)
    elif isinstance(value, list):
        for item in value: yield from mappings(item)


def replace_slide(folder, number, body, kind, note):
    path = folder/'presentation.md'
    text = path.read_text()
    # These imported trial sources have no --- inside code fences.
    slides = text.split('\n\n---\n\n')
    year = folder.name.rsplit('-', 1)[-1]
    expected = f'<!-- Source: Rails World {year}, slide {number} -->'
    if not 1 <= number <= len(slides) or expected not in slides[number - 1]:
        raise ValueError(f'{folder.name}: source slide {number} does not match the expected trial')
    prefix = slides[number-1].split('-->', 1)[0] + '-->'
    slides[number-1] = prefix + '\n\n' + body.strip()
    path.write_text('\n\n---\n\n'.join(slides).rstrip()+'\n')
    report_path = folder/'import-report.json'
    report = json.loads(report_path.read_text())
    entry = report['slides'][number-1]
    entry['kind'] = kind
    entry['notes'] = [note] if note else []
    report_path.write_text(json.dumps(report, indent=2)+'\n')


def refine(root, unpacked, originals):
    if unpacked:
        document = objects(unpacked/'Index/Document.iwa.yaml')
        metadata = yaml.safe_load((unpacked/'Index/Metadata.iwa.yaml').read_text())
        files = dict(mappings(metadata))
        show = next(o for o in document.values() if o['_pbtype'] == 'KN.ShowArchive')
        for number, ref in enumerate(show['slideTree']['slides'], 1):
            node = document[ref['identifier']]
            slide_id = node['slide']['identifier']
            path = unpacked/f'Index/Slide-{slide_id}.iwa.yaml'
            if not path.exists(): continue
            contents = objects(path)
            for item in contents.values():
                if item['_pbtype'] == 'TSD.MovieArchive':
                    info = files[item['movieData']['identifier']]
                    name = info.get('preferredFileName', info['fileName'])
                    dest = root/'rails-world-2023'/'videos'/name
                    shutil.copyfile(unpacked/'Data'/info['fileName'], dest)
                    replace_slide(root/'rails-world-2023',number,f'![](<{name}>)','video',
                                  'Recovered from original Keynote movie reference; LibreOffice omitted this movie')
                elif number == 35 and item['_pbtype'] == 'TSD.ImageArchive':
                    info = files[(item.get('originalSVGData') or item['data'])['identifier']]
                    name = info.get('preferredFileName',info['fileName'])
                    shutil.copyfile(unpacked/'Data'/info['fileName'],root/'rails-world-2023'/'images'/name)
                    replace_slide(root/'rails-world-2023',number,f'![fit](<{name}>)','artwork',
                                  'Recovered SVG from original Keynote; LibreOffice omitted this image')
    # This three-slide build places the same illustration beside growing text.
    import re
    parts=(root/'rails-world-2024/presentation.md').read_text().split('\n\n---\n\n')
    for number in (32,33,34):
        match = re.search(r'!\[[^\]]*\]\(([^)]+)\)', parts[number-1])
        if not match:
            raise ValueError(f'Rails World 2024 slide {number}: expected artwork is missing')
        image = match.group(1)
        body=f'![left]({image})\n\n# Are YOU suffering from server-phobia?'
        if number>=33: body+='\n\nDon’t worry. There’s a cure.'
        if number>=34: body+='\n\nIt’s called **LINUX**.'
        replace_slide(root/'rails-world-2024',number,body,'native-text-and-artwork','Source-backed image-left layout, with themed text')
    replace_slide(root/'rails-world-2024',90,'<!-- Intentional blank transition slide in the source PDF -->','blank','Verified black/blank source PDF page; uses the selected theme background')
    replace_slide(root/'rails-world-2025',13,'# Markdown\n\nLingua Franca of AI','native-text','Headline/subtitle inferred from source')
    replace_slide(root/'rails-world-2025',60,'''# Bootstrap Budget

| Time | Task |
| ---: | :--- |
| 5m | Install OS |
| 3m | Setup app |
| 2m | Run Local CI |
| 3m | Deploy code |''','native-table','Original slide text represented as a themed Markdown table')
    replace_slide(root/'rails-world-2025',61,''' > People who are really serious about software should make their own [operating system].

— Alan Kay’ish'''.strip(),'native-quote','Quote and attribution inferred from source')
    code = (originals/'2025/Rails World 2025/code.rb').read_text()
    start = code.index('class ProcessImportJob')
    end = code.find('\n\n\n', start)
    if end < 0:
        end = len(code)
    replace_slide(root/'rails-world-2025',25,'```ruby\n'+code[start:end].strip()+'\n```','native-code',
                  'Replaced screenshot with matching ProcessImportJob from the deck’s code.rb')
    # Keep the external demo alongside the deck, but do not invent a source slide
    # association: the archived PPTX/ODP do not contain a movie relationship.
    demo = originals/'2024/Rails World 2024/RW Demo Final.mp4'
    if demo.exists():
        shutil.copyfile(demo,root/'rails-world-2024/videos'/demo.name)
        (root/'rails-world-2024/video-demo.md').write_text('---\ntitle: Rails World 2024 — external demo\ntheme: tokyo-night\n---\n\n![](<RW Demo Final.mp4>)\n')

def run(root, unpacked, originals):
    names = [f'rails-world-{year}' for year in (2023, 2024, 2025)]
    root = root.resolve()
    # All mutations happen on copies. Missing source assets, code, or unexpected
    # slide structure can never leave an existing trial half-refined.
    with tempfile.TemporaryDirectory(prefix='.hype-refine-', dir=root.parent) as staging:
        staging = Path(staging)
        for name in names:
            shutil.copytree(root / name, staging / name)
        refine(staging, unpacked, originals)
        publish_directories(staging, root, names)


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path('trials'))
    p.add_argument('--keynote-unpacked',type=Path)
    p.add_argument('--originals',type=Path,default=Path.home()/'Dropbox/Documents/Presentations')
    a=p.parse_args();run(a.root,a.keynote_unpacked,a.originals)

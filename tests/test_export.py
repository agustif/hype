"""Exercise embedded movie relationships, playback flags, and atomic export."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile
from PIL import Image
from lxml import etree

spec=importlib.util.spec_from_file_location('export_pptx',Path(__file__).resolve().parents[1]/'tools/export_pptx.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class ExportTests(unittest.TestCase):
    def test_movie_is_embedded_and_failure_preserves_destination(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            Image.new('RGB',(1920,1080),'#1a1b26').save(root/'slide.png')
            Image.new('RGB',(320,180),'#7aa2f7').save(root/'poster.png')
            subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','color=c=blue:s=320x180:d=0.2',
                            '-c:v','libx264','-pix_fmt','yuv420p',str(root/'video.mp4')],check=True)
            data={'title':'Test','slides':[{'image':'slide.png','video':str(root/'video.mp4'),
                  'poster':str(root/'poster.png'),'span':False,'title':False,'autoplay':True,'loop':True,'muted':True}]}
            manifest=root/'slides.json';manifest.write_text(json.dumps(data))
            output=root/'talk.pptx';module.export(manifest,output)
            with zipfile.ZipFile(output) as archive:
                self.assertTrue(any(n.endswith('.mp4') for n in archive.namelist()))
                xml=etree.fromstring(archive.read('ppt/slides/slide1.xml'))
                ns={'p':'http://schemas.openxmlformats.org/presentationml/2006/main'}
                node=xml.find('.//p:video/p:cMediaNode',ns)
                self.assertIsNotNone(node);self.assertEqual(node.get('vol'),'0')
                timing=node.find('p:cTn',ns);self.assertEqual(timing.get('repeatCount'),'indefinite')
                self.assertEqual(timing.find('p:stCondLst/p:cond',ns).get('delay'),'0')
                rels=archive.read('ppt/slides/_rels/slide1.xml.rels')
                self.assertNotIn(b'TargetMode="External"',rels)
            original=output.read_bytes()
            data['slides'][0]['video']=str(root/'missing.mp4');manifest.write_text(json.dumps(data))
            with self.assertRaises(Exception): module.export(manifest,output)
            self.assertEqual(output.read_bytes(),original)

if __name__=='__main__': unittest.main()

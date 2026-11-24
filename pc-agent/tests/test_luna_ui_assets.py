"""Resource/partition checks; no board, media actions or serial port involved."""
import hashlib
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[2]
ASSETS=ROOT/'firmware/luna-panel/main/assets'

class UiAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest=json.loads((ASSETS/'manifest.json').read_text(encoding='utf-8'))

    def test_fonts_match_generated_hashes(self):
        for name,description in self.manifest['fonts'].items():
            with self.subTest(font=name):
                self.assertEqual(hashlib.sha256((ASSETS/(name+'.c')).read_bytes()).hexdigest(),description['sha256'])

    def test_compression_uses_guarded_decoder_only_for_large_fonts(self):
        for name,description in self.manifest['fonts'].items():
            source=(ASSETS/(name+'.c')).read_text(encoding='utf-8')
            compressed=name in ('luna_cjk_16','luna_cjk_28')
            self.assertEqual(description['compressed'],compressed)
            self.assertIn('.get_glyph_bitmap = '+('luna_font_bitmap_get' if compressed else 'lv_font_get_bitmap_fmt_txt'),source)

    def test_full_fonts_cover_static_text_and_rare_titles(self):
        text=(ROOT/'firmware/luna-panel/main/luna_preview_ui.c').read_text(encoding='utf-8')
        for name in ('luna_cjk_16','luna_cjk_28'):
            chars=set(self.manifest['fonts'][name]['codepoints'])
            self.assertEqual(len(chars),31031)
            self.assertTrue({ord(c) for c in text if ord(c)>127} <= chars)
            self.assertTrue({ord(c) for c in '上海龘喵星雾晴'} <= chars)
            self.assertIn(0x20087,chars)

    def test_b1_partition_preserves_non_app_regions(self):
        def read_table(name):
            cursor=0x11000
            table={}
            for line in (ROOT/'firmware/luna-panel'/name).read_text(encoding='utf-8').splitlines():
                if not line.strip() or line.startswith('#'): continue
                fields=[p.strip() for p in line.split(',')]
                region,kind,subtype,offset,size=fields[:5]
                size=int(size[:-1])*1024*1024 if size.endswith('M') else int(size,0)
                alignment=0x10000 if kind=='app' else 0x1000
                offset=int(offset,0) if offset else (cursor+alignment-1)//alignment*alignment
                table[region]=(kind,subtype,offset,size)
                cursor=offset+size
            return table
        original=read_table('partitions.csv'); b1=read_table('partitions.ble_b1.csv')
        for region in ('nvs','phy_init','storage'): self.assertEqual(original[region],b1[region])
        self.assertEqual(original['factory'][2],b1['factory'][2])
        self.assertEqual(b1['factory'][2]+b1['factory'][3],b1['storage'][2])
        self.assertEqual(original['factory'][3],6*1024*1024)
        self.assertEqual(b1['factory'][3],10*1024*1024)

    def test_exact_approved_asset_geometry(self):
        images=self.manifest['images']
        self.assertEqual((images['luna_backdrop']['width'],images['luna_backdrop']['height']),(720,720))
        self.assertEqual(images['luna_cloud']['height'],105)
        for frame in range(8):
            self.assertEqual((images[f'luna_pet_{frame}']['width'],images[f'luna_pet_{frame}']['height']),(72,72))
        self.assertEqual(self.manifest['converter'],'lv_font_conv@1.5.3')

if __name__=='__main__': unittest.main()

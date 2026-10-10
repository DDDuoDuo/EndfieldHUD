#!/usr/bin/env python3
"""Copy the two source textures the Personal Profile artwork derives from.

The Mac source decodes business_card_topic_normal_1 (HUDSourceWatchView
profileBackgroundArtwork) and icon_user_avatar_frame_endfield_1 (HUDPortraitArtwork
sourceFrame) from their decoded BGRA mips. Windows regenerates the themed card,
hover plate and tinted portrait frame from the same bytes, copied verbatim into
windows/resources/profile with a catalog of sizes, sprite rects and digests.
"""
from pathlib import Path
import hashlib, json, shutil

root = Path(__file__).resolve().parents[2]
out = root / 'windows/resources/profile'
out.mkdir(parents=True, exist_ok=True)
textures = [
    ('business-card.bgra', 'Resources/WatchSource/Textures/business_card_topic_normal_1--4a226705---4827637915678035611.bgra-mips.bin', 532, 204, [0, 0, 530, 204],
     'desktop-profile-card.json business_card_topic_normal_1: raw m_Rect 530x204 at textureRect - textureRectOffset'),
    ('avatar-frame.bgra', 'Resources/WatchSource/Textures/icon_user_avatar_frame_endfield_1--63c7ff92--7647223879671712896.bgra-mips.bin', 256, 256, [1, 1, 254, 254],
     'HUDPortraitArtwork.sourceFrame: the untrimmed 254x254 HeadFrameImg sprite rect'),
]
files = []
for name, source, width, height, sprite, rule in textures:
    data = (root / source).read_bytes()
    assert len(data) >= width * height * 4 and not data.startswith(b'EHUDZ01\0'), source
    shutil.copyfile(root / source, out / name)
    files.append({'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'source': source,
                  'textureWidth': width, 'textureHeight': height, 'spriteRect': sprite, 'rowOrigin': 'bottom', 'channels': 'BGRA8 straight', 'rule': rule})
catalog = {'schema': 1, 'files': files,
           # Mac export of desktop.profile.background (accent FAD41F, no photo): texturePixels(themedBackgroundArtwork(card)).
           'defaultBackgroundTextureSHA256': 'b056447d3d740241d9b677948b23ccb9c110d334a91e116e984a09090a7a4888',
           'policy': 'Shared game assets used by the macOS source only; byte-identical copies.'}
(out / 'catalog.json').write_text(json.dumps(catalog, indent=2, sort_keys=True) + '\n')
print('Wrote', out, [f['path'] for f in files])

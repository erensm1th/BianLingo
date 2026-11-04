"""Build application and addon icons, plus the independent pixel settings button."""
from pathlib import Path
from io import BytesIO
import hashlib
import json
ROOT=Path(__file__).resolve().parents[1]
def artwork():
    from PIL import Image
    image=Image.open(ROOT/"resources/branding/logo.png").convert("RGBA")
    box=image.getchannel("A").getbbox()
    if box is None: raise ValueError("Artwork is fully transparent")
    side=max(box[2]-box[0],box[3]-box[1])+8
    x=(box[0]+box[2]-side)//2; y=(box[1]+box[3]-side)//2
    return image.crop((x,y,x+side,y+side))
def make_icon():
    from PIL import Image
    image=artwork()
    output=BytesIO()
    image.save(output,format='ICO',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(96,96),(128,128),(256,256)])
    return output.getvalue()
def make_settings_icon():
    from PIL import Image
    source = Image.open(ROOT / "resources/branding/settings-icon.png").convert("RGBA")
    if min(source.size) < 1 or max(source.size) > 1024:
        raise ValueError("Settings artwork must be between 1 and 1024 pixels per side")
    # Keep the supplied pixels and alpha unchanged; add only a transparent border.
    side = max(source.size) + 2
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(source, ((side - source.width) // 2, (side - source.height) // 2))
    source = canvas
    sizes = [16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 96, 128, 256]
    images = [source.resize((n, n), Image.Resampling.NEAREST) for n in sizes]
    output = BytesIO()
    images[-1].save(output, format="ICO", sizes=[(n, n) for n in sizes], append_images=images[:-1])
    return output.getvalue()

if __name__=='__main__':
    from PIL import Image
    (ROOT/'resources/bianlingo.ico').write_bytes(make_icon())
    artwork().resize((64,64),Image.Resampling.LANCZOS).save(ROOT/'addon/BianLingo/Textures/Logo.tga')
    (ROOT/'resources/settings-icon.ico').write_bytes(make_settings_icon())
    source=ROOT/'resources/branding/logo.png'
    manifest={'source':source.relative_to(ROOT).as_posix(),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'artwork':'Original hand-drawn two-cat composition preserved; empty exterior margins trimmed in icon derivatives',
        'settings_source':'resources/branding/settings-icon.png',
        'settings_source_sha256':hashlib.sha256((ROOT/'resources/branding/settings-icon.png').read_bytes()).hexdigest(),
        'derivatives':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in ('resources/bianlingo.ico','resources/settings-icon.ico','addon/BianLingo/Textures/Logo.tga')}}
    (ROOT/'resources/branding/manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')

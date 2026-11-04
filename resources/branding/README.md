# BianLingo artwork

`logo.png` is the original hand-drawn composition: a cream-colored cat and a gray cat curled together. The cream/white regions and paws belong to the artwork. The application, tray and addon icons preserve the composition; only transparent exterior margins are trimmed.

`settings-icon.png` is the supplied pixel artwork used by the overlay's top-right settings button. Edit or replace this PNG to update that button without changing the main logo. Rectangular transparent PNGs up to 1024 pixels per side are supported. Original artwork pixels and alpha are preserved; generated icons add a transparent square canvas and use nearest-neighbor resizing.

Run `python tools/generate_project_icon.py` before building. It generates `resources/bianlingo.ico`, `resources/settings-icon.ico`, `addon/BianLingo/Textures/Logo.tga` and their hash manifest. The pixel button uses nearest-neighbor resizing at common Windows DPI sizes, without smoothing or a background tile. The hand-drawn source remains unchanged.

# BianLingo artwork

`logo.png` is the original hand-drawn composition: a cream-colored cat and a gray cat curled together. The cream/white regions and paws belong to the artwork. The application, tray and addon icons preserve the composition; only transparent exterior margins are trimmed.

`settings-icon.png` is the separate **24 × 24 pixel icon used by the overlay’s top-right settings button**. It has a cream outer silhouette, a gray cat and a transparent background. Edit or replace this PNG to update that button without changing the main logo. Keep it square, no larger than 256 × 256, with fully opaque or fully transparent pixels and an empty outermost pixel border.

Run `python tools/generate_project_icon.py` before building. It generates `resources/bianlingo.ico`, `resources/settings-icon.ico`, `addon/BianLingo/Textures/Logo.tga` and their hash manifest. The pixel button uses nearest-neighbor resizing at common Windows DPI sizes, without smoothing or a background tile. The hand-drawn source remains unchanged.

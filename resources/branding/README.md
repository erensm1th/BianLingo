# BianLingo artwork

`logo.png` is the original hand-drawn composition: a cream-colored cat and a gray cat curled together. The cream/white regions and paws belong to the artwork. The application, tray and addon icons preserve the composition; only transparent exterior margins are trimmed.

`settings-icon.png` is the supplied pixel artwork used by the overlay's top-right settings button. Edit or replace this PNG to update that button without changing the main logo. Rectangular transparent PNGs up to 1024 pixels per side are supported. Original artwork pixels and alpha are preserved; generated icons add a transparent square canvas and use nearest-neighbor resizing.

Run `python tools/generate_project_icon.py` before building. It generates `resources/bianlingo.ico`, `resources/settings-icon.ico`, `addon/BianLingo/Textures/Logo.tga` and their hash manifest. The pixel button uses nearest-neighbor resizing at common Windows DPI sizes, without smoothing or a background tile. The hand-drawn source remains unchanged.

`duanduan.png` is the supplied 40×34 transparent pixel portrait of Duanduan, the cream cat. It appears beside the reply/output translation heading and the in-game addon settings heading, and as a larger portrait at the top-right of the settings content header. The generator produces `resources/duanduan.ico` and `addon/BianLingo/Textures/Duanduan.tga`, preserving colors and alpha with transparent square padding and nearest-neighbor resizing. Replace this source PNG and rerun the generator to update Duanduan's appearance.

`bianbian.png` is the supplied gray-cat pixel portrait. Its `resources/bianbian.ico` derivative appears above the version label in the Windows settings sidebar. Duanduan has a separate position in the top-right content header. Both portraits use transparent backgrounds and nearest-neighbor scaling. They shrink or hide when their respective layouts leave insufficient room; they do not add input controls or animation. The settings window caption uses the application logo.

# Corresponding source

The Windows package includes its local components, original notices and matching
source archive in the `data` directory. The helper at `data/bergamot/BianLingoBergamot.exe`
is accompanied by `corresponding-source.zip` and `source-manifest.json` in the
same directory. Unzip that
archive to obtain the actual patched Bergamot source, initialized dependencies,
PCRE2 source, independent wrapper, build scripts, original license texts and build
instructions. The matching source is included in the package; no account is required. The manifest binds the archive to the helper binary by SHA-256.

Bergamot's covered files and modifications remain under MPL-2.0. Other libraries
retain their own terms, and the independent project wrapper is MIT. The archive
may include unbuilt upstream tools/tests and their licenses; this does not imply
they are linked into the application. Preserve their notices when redistributing
the source archive. Nothing in the application license restricts your upstream
source rights. The main application remains MIT licensed.

Mozilla model downloads are pinned in `data/bergamot/models.json` and in
`resources/bergamot-models.json` in the project source. These identify the actual
model files, upstream locations and SHA-256, under Mozilla's stated MPL-2.0 model
license. They are model artifacts, separate from the helper source archive.

Redistributed binaries must retain their matching source archive and notices.
The application source is available at https://github.com/erensm1th/BianLingo.

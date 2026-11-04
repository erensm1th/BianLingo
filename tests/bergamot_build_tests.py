"""Fresh and repeated application of the pinned upstream compatibility patches."""
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from tools.build_bergamot import patch_exact


class UpstreamPatchTests(unittest.TestCase):
    def test_replacements_can_shrink_wrap_or_replace_original_text(self):
        cases = [
            ("#pragma GCC push\n#include ruy\n#pragma GCC pop", "#include ruy"),
            ("#if HAVE_HASH", "#undef HAVE_HASH\n#if HAVE_HASH"),
            ("pcre2-8.lib", "pcre2-8-static.lib"),
        ]
        with TemporaryDirectory() as directory:
            path = Path(directory) / "upstream.h"
            for old, new in cases:
                with self.subTest(old=old):
                    path.write_text("prefix\n" + old + "\nsuffix", encoding="utf8")
                    patch_exact(path, old, new)
                    expected = "prefix\n" + new + "\nsuffix"
                    self.assertEqual(path.read_text(encoding="utf8"), expected)
                    patch_exact(path, old, new)
                    self.assertEqual(path.read_text(encoding="utf8"), expected)

    def test_changed_or_ambiguous_upstream_is_rejected_without_writing(self):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "upstream.h"
            for data in ("unexpected", "original\noriginal"):
                path.write_text(data, encoding="utf8")
                with self.assertRaises(ValueError):
                    patch_exact(path, "original", "replacement")
                self.assertEqual(path.read_text(encoding="utf8"), data)


if __name__ == "__main__":
    unittest.main()

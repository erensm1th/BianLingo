---
license: apache-2.0
tags:
- translation
- opus-mt
- ctranslate2
- base
source_model: Helsinki-NLP/opus-mt-en-ar
---

# ooeoeo/opus-mt-en-ar-ct2-float16

CTranslate2 float16 quantized version of `Helsinki-NLP/opus-mt-en-ar`.

Converted for use in the [ooeoeo](https://ooeoeo.com) desktop engine
with the `opus-mt-server` inference runtime.

## Source

- Upstream model: [Helsinki-NLP/opus-mt-en-ar](https://huggingface.co/Helsinki-NLP/opus-mt-en-ar)
- Variant: `base`
- Language pair: `en-ar`

## Conversion

```bash
ct2-transformers-converter \
    --model Helsinki-NLP/opus-mt-en-ar \
    --output_dir ct2/ \
    --quantization float16 \
    --copy_files source.spm target.spm tokenizer_config.json vocab.json
```

## License

Apache-2.0 (inherited from upstream Helsinki-NLP/Opus-MT).

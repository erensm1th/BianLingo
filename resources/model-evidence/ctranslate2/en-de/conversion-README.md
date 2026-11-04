---
license: apache-2.0
tags:
- translation
- opus-mt
- ctranslate2
- base
source_model: Helsinki-NLP/opus-mt-en-de
---

# ooeoeo/opus-mt-en-de-ct2-float16

CTranslate2 float16 quantized version of `Helsinki-NLP/opus-mt-en-de`.

Converted for use in the [ooeoeo](https://ooeoeo.com) desktop engine
with the `opus-mt-server` inference runtime.

## Source

- Upstream model: [Helsinki-NLP/opus-mt-en-de](https://huggingface.co/Helsinki-NLP/opus-mt-en-de)
- Variant: `base`
- Language pair: `en-de`

## Conversion

```bash
ct2-transformers-converter \
    --model Helsinki-NLP/opus-mt-en-de \
    --output_dir ct2/ \
    --quantization float16 \
    --copy_files source.spm target.spm tokenizer_config.json vocab.json
```

## License

Apache-2.0 (inherited from upstream Helsinki-NLP/Opus-MT).

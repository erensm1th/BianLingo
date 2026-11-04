---
license: apache-2.0
tags:
- translation
- opus-mt
- ctranslate2
- base
source_model: Helsinki-NLP/opus-mt-en-id
---

# ooeoeo/opus-mt-en-id-ct2-float16

CTranslate2 float16 quantized version of `Helsinki-NLP/opus-mt-en-id`.

Converted for use in the [ooeoeo](https://ooeoeo.com) desktop engine
with the `opus-mt-server` inference runtime.

## Source

- Upstream model: [Helsinki-NLP/opus-mt-en-id](https://huggingface.co/Helsinki-NLP/opus-mt-en-id)
- Variant: `base`
- Language pair: `en-id`

## Conversion

```bash
ct2-transformers-converter \
    --model Helsinki-NLP/opus-mt-en-id \
    --output_dir ct2/ \
    --quantization float16 \
    --copy_files source.spm target.spm tokenizer_config.json vocab.json
```

## License

Apache-2.0 (inherited from upstream Helsinki-NLP/Opus-MT).

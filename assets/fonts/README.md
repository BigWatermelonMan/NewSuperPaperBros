# Badges tab lettering

`super-mario-256/SuperMario256.ttf` is Super Mario 256 by fsuarez913. The creator's listing describes it as a replica of Mario lettering and lists it as **100% Free**:

https://www.dafont.com/super-mario-256.font

Downloaded from the creator's download link on 2026-10-25. This is a matching block-style typeface, not a claim that the game shipped this exact TTF. It replaces the visibly different dialogue lettering on the Badges tab. The fixed word is baked at an 18-pixel ink height into native GX alpha runs by `tools/make-badge-label.py`; the game does not need the TTF installed or loaded at runtime. The supplied font is kept intact for reproducible generation.

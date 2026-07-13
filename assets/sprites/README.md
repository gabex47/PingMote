# Sprite assets

Sprite packs will live in namespaced subdirectories with one animation strip per state:

`idle`, `talking`, `listening`, `thinking`, `sleeping`, and `bounce`.

Use lossless PNG with premultiplied alpha and include a small metadata file containing frame size, frame duration, and loop behavior. The procedural renderer remains the fallback when a pack is missing or invalid.

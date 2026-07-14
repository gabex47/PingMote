# Sprite assets

Sprite packs will live in namespaced subdirectories with one animation strip per state:

`idle`, `talking`, `listening`, `thinking`, `sleeping`, and `bounce`.

Use lossless PNG with premultiplied alpha. A horizontal strip is detected automatically when its width is an exact multiple of its height; every square is treated as one animation frame. Missing states reuse the closest loaded state, and missing files are logged without terminating the application.

# f1_gearari

The low-poly F1 car used by the viewer (`apps/viewer/car_model.cpp`).

- `body.glb`: body, origin on the ground at the wheelbase centre, +Z forward, +X = car's left.
- `wheel_front.glb` / `wheel_rear.glb`: one wheel each (tyre, rim, brake disc, nut, decal), pivot at the hub,
  axle along +X. Rotate 180° about Y for right-side wheels. Hub positions are in `car.json`.
- Paint is one material, `livery`, using a 2048×2048 texture:
  - `livery_default.png`: the model's original colours.
  - `liveries/*.png`: ready-made schemes.

The viewer finds the livery material by its 2048-wide texture (raylib drops material names) and swaps in
one `liveries/*.png` per car.

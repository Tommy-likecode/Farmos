# M5 physics reference (NOT product code)

`physics_ref.py` is the numeric oracle for M5 fixtures. It is not linked into
Farmos, not shipped as stdlib, and not an acceptable substitute for the C
implementation poteto writes.

```
python3 spec/tests/M5/_ref/physics_ref.py
```

prints smoke values used when baking `.expected` files.

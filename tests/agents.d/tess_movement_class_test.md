# tess_movement_class_test

- `tess_movement_class_test`: verifies the compile-time movement vocabulary
  (`tess::movement`): the `MovementClassFor` concept and `movement_class_of`
  tag/class normalization, source-compatible default step policies, stable
  step-policy identifiers, shape/policy compatibility validation, byte-exact
  `normalize_cost` (zero and negative are
  impassable, overflow saturates through a u64 compare), composed passability
  truth tables for a Walker (`AllOf<Field, Not<Field>>`) versus a Builder
  (`AnyOf`) over construction tiles, per-class entry-cost expressions
  (`FieldCost`, `SelectCost`, `ConstantCost`), and that the `WalkableField`
  identity class reproduces the legacy `static_cast<bool>(field)` result and
  exposes the same `field_span` storage the region flood scans.

# tess_no_exceptions_consumer_contract_test

- `tess_no_exceptions_consumer_contract_test`: compiles the forward, reverse,
  and leaf-first public-header consumer contract with the exception-disabled
  compiler recipe.
  `no_exceptions_manifest.json` (schema 2) classifies every subsystem exactly
  once: `affected_subsystems` maps each one to its exception-free runtime
  cases, and `unaffected_subsystems` records the rest with a written reason.
  `tools/check_no_exceptions_manifest.py` derives the subsystem set from the
  directories under `include/tess`, so adding a subsystem fails validation
  until the manifest classifies it. It also verifies every mapped case and
  its named enabled-mode counterpart.

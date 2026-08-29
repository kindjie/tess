# test_check_page_descriptions.py

- `tests/test_check_page_descriptions.py`: synthetic built-site coverage
  for the maintained-page description requirement. A specific
  description passes; the generic site fallback, a missing description,
  and a duplicated one each fail with the page named; the generated
  `api/`/`demo/` trees and the 404 template are out of scope (their
  contracts live elsewhere); and an enumeration that visits no
  maintained page fails rather than passing vacuously.

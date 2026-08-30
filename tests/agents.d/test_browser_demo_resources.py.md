# test_browser_demo_resources.py

- `tests/test_browser_demo_resources.py`: fault-injects every browser-page
  acquisition boundary so partial construction and repeated closure cannot
  leak a profile, process, DevTools connection, or pre-upgrade socket. It also
  pins the bounded DevTools read timeout needed by software-GPU browser tests.

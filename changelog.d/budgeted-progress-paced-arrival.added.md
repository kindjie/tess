- The budgeted-progress suite gains paced arrival cells: frames wait
  for their 60 FPS edges on the monotonic clock, so artifacts carry
  measured completions-per-wall-second, the measured wall span, and
  the frame-start-lag distribution — the design's adopter-facing rate
  claim, published from paced cells exclusively (unpaced artifacts
  must omit wall rates, enforced by the fail-closed validator).

- Prevented superseded CI runs from opening a failure issue when their only
  failure is the aggregate gate reacting to a cancelled dependency, while
  retaining the alert unless the Actions API confirms a newer equivalent push
  run.

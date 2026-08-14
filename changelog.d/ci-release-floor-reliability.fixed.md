- Restore the release-floor CI evidence by isolating ccache-free jobs from the
  workflow launcher, selecting the Visual Studio 2022 runner for the MSVC
  19.44 floor, and reporting every non-pull-request job failure in the rolling
  CI issue.

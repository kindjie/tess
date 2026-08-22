## 2026-08-21 - Sequence central package registries around 1.0

- Deferred a curated vcpkg port until general availability. vcpkg's maintainer
  guide admits a project to the curated registry only once it has a release at
  least six months old, six months of active public development, or equivalent
  demonstrated maturity. This repository's first release published
  2026-07-18, so the earliest qualifying date is around 2027-01-18, which is
  close to the projected GA window. This reinforces rather than changes the
  standing decision that the in-tree overlay stays checkout-based through GA.
- Recorded a naming risk to settle before that port is authored. vcpkg's
  distinctive-name policy treats short, common-word names that do not lead to
  the project in a search engine as ambiguous, and its documented remedy is an
  owner prefix. `tess` is short, is a common given name, and collides with an
  established OCR project's usual abbreviation, so expect a rename request.
- Selected Conan Center as the near-term registry. Its documented requirements
  cover recipe quality only, with no maturity or popularity gate, and it
  accepts new recipes continuously, including header-only C++20 libraries.
- Fixed the shape a central Conan recipe must take, so it is not mistaken for
  a move of the in-tree one. A registry recipe downloads the tagged source
  archive through `conandata.yml` rather than exporting a checkout; deletes
  the installed CMake package files so the registry's own generator supplies
  them; and points `url` at the registry with `homepage` at this project. The
  in-tree `conanfile.py` stays exactly as it is, because release CI packages
  the checkout under test. The two recipes are separate artifacts.
- Established that registries hash the tag's source archive rather than a
  release's portable-headers assets, which deliberately carry no build files.
  A future release may attach a deterministic source archive so a registry can
  hash a published asset instead; a published release cannot be amended to add
  one after the fact.
- Validated a candidate Conan Center recipe against the `v0.13.0` tag archive
  on 2026-08-21, including a package build and its test package. It is held
  pending a submission decision and is not part of this repository.
- Noted the submission sequence Conan Center requires: an issue before the
  pull request, a signed contributor agreement, a branch named for the
  package, one recipe per pull request, and only the released version in the
  initial submission.

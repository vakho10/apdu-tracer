# Changelog

All notable changes to this project are documented in this file.

## [Unreleased]

### Bug Fixes

- Stop publishing release assets twice

### Documentation

- Update README for the renderer split and tooling

## [1.0.2] - 2026-05-30

### Bug Fixes

- Fix per-device ATR gating and empty trace import

### Features

- Add MIT LICENSE file
- Add ESLint and Prettier with a formatting pass
- Add Vitest and a decoder test suite
- Add CI workflow for typecheck, lint, format and tests
- Enable renderer sandbox and tighten the CSP

### Miscellaneous

- Pin LF line endings via .gitattributes

### Refactor

- Maintain running summary aggregates instead of re-parsing the trace
- Split renderer into dom, cards and summary modules

## [1.0.1] - 2026-05-23

### Documentation

- Document the release process in the README

### Features

- Add application icon and bump to v1.0.1

## [1.0.0] - 2026-05-23

### Features

- Add GitHub Actions release workflow for Windows builds


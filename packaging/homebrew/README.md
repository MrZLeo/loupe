# Homebrew packaging

Loupe is distributed from the `mrzleo/homebrew-tap` tap. The Formula installs
the versioned, checksummed macOS universal binary produced by the tagged
release workflow.

Generate a Formula for an existing stable release with:

```sh
ruby packaging/homebrew/generate-formula.rb \
  v0.3.0 \
  packaging/homebrew/testdata/v0.3.0-SHA256SUMS \
  /tmp/loupe.rb
```

The release workflow performs the same operation with the checksums from the
new release, uploads `loupe.rb` as a release asset, and updates the tap when the
`HOMEBREW_TAP_TOKEN` repository secret is configured. Use a fine-grained token
with Contents read/write access to `mrzleo/homebrew-tap`.

The tap repository is already initialized at `mrzleo/homebrew-tap`. The release
workflow updates `Formula/loupe.rb`, so no bootstrap step is required.

To validate a Formula in a local tap:

```sh
brew tap-new --no-git mrzleo/loupe-test
cp /tmp/loupe.rb "$(brew --repository mrzleo/loupe-test)/Formula/loupe.rb"
brew style --formula mrzleo/loupe-test/loupe
brew audit --strict --formula mrzleo/loupe-test/loupe
brew install mrzleo/loupe-test/loupe
brew test mrzleo/loupe-test/loupe
```

The checked-in `v0.3.0` checksum fixture is intentionally immutable. It keeps
the generator and both supported macOS architectures testable without depending
on an unreleased version.

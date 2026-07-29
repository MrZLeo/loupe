# frozen_string_literal: true

require "minitest/autorun"
require "tmpdir"

require_relative "generate-formula"

class GenerateFormulaTest < Minitest::Test
  MACOS_SHA256 = "a" * 64
  LICENSE_SHA256 = "d" * 64

  def setup
    @temporary_directory = Dir.mktmpdir("loupe-homebrew-test")
    @checksums_path = File.join(@temporary_directory, "SHA256SUMS")
    File.write(
      @checksums_path,
      <<~CHECKSUMS,
        #{MACOS_SHA256}  loupe-v1.2.3-macos-universal.tar.gz
        #{"b" * 64}  loupe-v1.2.3-linux-x86_64.tar.gz
        #{"c" * 64}  loupe-v1.2.3-windows-x86_64.zip
      CHECKSUMS
    )
  end

  def teardown
    FileUtils.remove_entry(@temporary_directory)
  end

  def test_renders_platform_artifacts_and_license
    formula = LoupeHomebrew.render(
      "v1.2.3",
      @checksums_path,
      license_sha256: LICENSE_SHA256,
    )

    assert_includes formula, "loupe-v1.2.3-macos-universal.tar.gz"
    assert_includes formula, %(sha256 "#{MACOS_SHA256}")
    assert_includes formula, %(sha256 "#{LICENSE_SHA256}")
    refute_match(/@[A-Z0-9_]+@/, formula)
  end

  def test_rejects_non_stable_tag
    error = assert_raises(ArgumentError) do
      LoupeHomebrew.render(
        "v1.2.3-rc.1",
        @checksums_path,
        license_sha256: LICENSE_SHA256,
      )
    end

    assert_match "stable semantic version", error.message
  end

  def test_requires_macos_checksum
    File.write(
      @checksums_path,
      "#{"b" * 64}  loupe-v1.2.3-linux-x86_64.tar.gz\n",
    )

    error = assert_raises(ArgumentError) do
      LoupeHomebrew.render(
        "v1.2.3",
        @checksums_path,
        license_sha256: LICENSE_SHA256,
      )
    end

    assert_match "loupe-v1.2.3-macos-universal.tar.gz", error.message
  end
end

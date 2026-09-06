# Build Directory

<!-- toc -->

- [1. Set required versions of compression libraries](#1-set-required-versions-of-compression-libraries)
- [2. Generate required package](#2-generate-required-package)

<!-- tocstop -->

Feel free to use the scripts in this directory to build a custom package for
Red Hat or Debian-based operating systems in a few simple steps.

These scripts and their corresponding `Dockerfiles` are utilized to generate
release artifacts via GitHub Actions.

## 1. Set required versions of compression libraries

Specify the required versions of the compression libraries in the
`build-common.sh` script.

## 2. Generate required package

Execute one of the following scripts to generate the target package:
- `build-deb.sh` — generates a `.deb` package;
- `build-rpm.sh` — generates an `.rpm` package.

Running these scripts displays a quick-start guide detailing the required
parameters and their valid values.

The resulting package is generated using the Docker `BuildKit` feature and
saved to the `packages` subdirectory.

Note that these scripts must be executed inside the `git` repository
to extract the specific version of the `pg_z` extension.

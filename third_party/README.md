# HOW TO BUILD STARCACHE THIRDPARTY

## Straight Forward Way

```
export INSTALL_DIR_PREFIX=</path/to/the/directory/to/install/header/and/libraries/>
./build-thirdparty.sh
```

If `INSTALL_DIR_PREFIX` environment variable is not set, `build-thirdparty.sh` will install all artifacts to `installed/` sub directory.

NOTE: `build-scripts/cmake-build.sh` will set `INSTALL_DIR_PREFIX` to environment variable `STARCACHE_THIRDPARTY` location and search for libraries to build.

## Provide Prebuilt Thirdparty Tarballs

Prebuilt thirdparty tarballs can't be used directly because some of the third party libraries are built by cmake and some build paths are hardcoded to cmake config file. For thirdparty shares among a group members, someone can build it and install to a common location, and others can point `STARCACHE_THIRDPARTY` environment variable to it. Project `build-scripts/cmake-build.sh` will use the environ variables and find those dependencies.

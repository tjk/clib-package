
#include "describe/describe.h"
#include "rimraf/rimraf.h"
#include "fs/fs.h"
#include "clib-package.h"


describe("clib_package_install_development", {
  it("should return -1 when given a bad package", {
    assert(-1 == clib_package_install_development(NULL, "./deps", 0));
  });

  it("should return -1 when given a bad dir", {
    clib_package_t *pkg = clib_package_new_from_slug("stephenmathieson/mkdirp.c", 0);
    assert(pkg);
    assert(-1 == clib_package_install_development(pkg, NULL, 0));
    clib_package_free(pkg);
  });

  it("should install the package's development dependencies", {
    clib_package_t *pkg = clib_package_new_from_slug("stephenmathieson/trim.c@0.0.2", 0);
    assert(pkg);
    assert(0 == clib_package_install_development(pkg, "./test/fixtures", 0));
    assert(0 == fs_exists("./test/fixtures/describe"));
    assert(0 == fs_exists("./test/fixtures/describe/describe.h"));
    assert(0 == fs_exists("./test/fixtures/describe/assertion-macros.h"));
    assert(0 == fs_exists("./test/fixtures/describe/package.json"));
    rimraf("./test/fixtures");
    clib_package_free(pkg);
  });
});


#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <stdio.h>
#include "parson.h"
#include "substr.h"
#include "http-get.h"
#include "mkdirp.h"
#include "fs.h"
#include "path-join.h"

#include "clib-package.h"

/**
 * Create a copy of the result of a `json_object_get_string`
 * invocation.  This allows us to `json_value_free()` the
 * parent `JSON_Value` without destroying the string.
 */

static inline char *
json_object_get_string_safe(JSON_Object *obj, const char *key) {
  const char *val = json_object_get_string(obj, key);
  if (!val) return NULL;
  return strdup(val);
}

/**
 * Create a copy of the result of a `json_array_get_string`
 * invocation.  This allows us to `json_value_free()` the
 * parent `JSON_Value` without destroying the string.
 */

static inline char *
json_array_get_string_safe(JSON_Array *array, int index) {
  const char *val = json_array_get_string(array, index);
  if (!val) return NULL;
  return strdup(val);
}

/**
 * Build a URL for `file` of the package belonging to `url`
 */

static inline char *
clib_package_file_url(const char *url, const char *file) {
  if (!url || !file) return NULL;

  int size =
      strlen(url)
    + 1  // /
    + strlen(file)
    + 1  // \0
    ;

  char *res = malloc(size);
  if (!res) return NULL;
  sprintf(res, "%s/%s", url, file);
  return res;
}

/**
 * Build a slug
 */

static inline char *
clib_package_slug(const char *author, const char *name, const char *version) {
  int size =
      strlen(author)
    + 1 // /
    + strlen(name)
    + 1 // @
    + strlen(version)
    + 1; // \0

  char *slug = malloc(size);
  sprintf(slug, "%s/%s@%s", author, name, version);
  return slug;
}

/**
 * Build a repo
 */

static inline char *
clib_package_repo(const char *author, const char *name) {
  int size =
      strlen(author)
    + 1 // /
    + strlen(name)
    + 1; // \0

  char *repo = malloc(size);
  sprintf(repo, "%s/%s", author, name);
  return repo;
}

/**
 * Create a new clib package from the given `json`
 */

clib_package_t *
clib_package_new(const char *json) {
  if (!json) return NULL;

  clib_package_t *pkg = malloc(sizeof(clib_package_t));
  if (!pkg) return NULL;

  pkg->src = NULL;
  pkg->dependencies = NULL;

  JSON_Value *root = json_parse_string(json);
  if (!root) {
    clib_package_free(pkg);
    return NULL;
  }

  JSON_Object *json_object = json_value_get_object(root);
  if (!json_object) {
    json_value_free(root);
    clib_package_free(pkg);
    return NULL;
  }

  pkg->json = json;
  pkg->name = json_object_get_string_safe(json_object, "name");

  pkg->repo = json_object_get_string_safe(json_object, "repo");
  // TODO how do we do this if there's no repo set?
  pkg->author = clib_package_parse_author(pkg->repo);
  // TODO hack.  yuck.
  pkg->repo_name = clib_package_parse_name(pkg->repo);
  // TODO support npm-style "repository"?

  pkg->version = json_object_get_string_safe(json_object, "version");
  pkg->license = json_object_get_string_safe(json_object, "license");
  pkg->description = json_object_get_string_safe(json_object, "description");
  pkg->install = json_object_get_string_safe(json_object, "install");

  JSON_Array *src = json_object_get_array(json_object, "src");
  if (src) {
    pkg->src = list_new();
    if (!pkg->src) {
      json_value_free(root);
      clib_package_free(pkg);
      return NULL;
    }

    for (size_t i = 0; i < json_array_get_count(src); i++) {
      char *file = json_array_get_string_safe(src, i);
      if (!file) break; // TODO fail
      list_node_t *node = list_node_new(file);
      list_rpush(pkg->src, node);
    }
  }

  JSON_Object *deps = json_object_get_object(json_object, "dependencies");
  if (deps) {
    pkg->dependencies = list_new();
    if (!pkg->dependencies) {
      json_value_free(root);
      clib_package_free(pkg);
      return NULL;
    }

    for (size_t i = 0; i < json_object_get_count(deps); i++) {
      const char *name = json_object_get_name(deps, i);
      if (!name) break; // TODO fail

      char *version = json_object_get_string_safe(deps, name);
      if (!version) break; // TODO fail

      clib_package_dependency_t *dep = clib_package_dependency_new(name, version);
      list_node_t *node = list_node_new(dep);
      list_rpush(pkg->dependencies, node);
    }
  }

  json_value_free(root);

  return pkg;
}

/**
 * Create a package from the given repo `slug`
 */

clib_package_t *
clib_package_new_from_slug(const char *_slug) {
  if (!_slug) return NULL;

  // sanatize `_slug`

  char *author = clib_package_parse_author(_slug);
  if (!author) return NULL;

  char *name = clib_package_parse_name(_slug);
  if (!name) {
    free(author);
    return NULL;
  }

  char *version = clib_package_parse_version(_slug);
  if (!version) {
    free(author);
    return NULL;
  }

  char *url = clib_package_url(author, name, version);
  if (!url) {
    free(author);
    return NULL;
  }

  char *json_url = clib_package_file_url(url, "package.json");
  if (!json_url) {
    free(author);
    free(url);
    return NULL;
  }

    // TODO rename response_t to http_get_response_t
  response_t *res = http_get(json_url);
  if (!res || !res->ok) {
    free(author);
    free(url);
    // if (res) free(res);
    return NULL;
  }

  clib_package_t *pkg = clib_package_new(res->data);
  if (pkg) {
    // we may install foo/bar@master which has the
    // version x.y.z specified in its package.json
    if (0 != strcmp(version, pkg->version)) {
      pkg->version = version;
    }
    char *repo = clib_package_repo(author, name);
    if (0 != strcmp(repo, pkg->repo)) {
      pkg->repo = repo;
    } else {
      free(repo);
    }
  }

  return pkg;
}

/**
 * Get a slug for the package `author/name@version`
 */

char *
clib_package_url(const char *author, const char *name, const char *version) {
  if (!author || !name || !version) return NULL;
  int size =
      23 // https://raw.github.com/
    + strlen(author)
    + 1 // /
    + strlen(name)
    + 1 // /
    + strlen(version)
    + 1 // \0
    ;

  char *slug = malloc(size);
  if (!slug) return NULL;

  sprintf(slug, "https://raw.github.com/%s/%s/%s", author, name, version);
  return slug;
}

/**
 * Parse the package author from the given `slug`
 */

char *
clib_package_parse_author(const char *slug) {
  char *copy;
  if (!slug || !(copy = strdup(slug))) return NULL;

  // if missing /, author = clibs
  char *name = strstr(copy, "/");
  if (!name) {
    free(copy);
    return CLIB_PACKAGE_DEFAULT_AUTHOR;
  }

  int delta = name - copy;
  char *author;
  if (!delta || !(author = malloc(delta))) {
    free(copy);
    return NULL;
  }

  author = substr(copy, 0, delta);
  free(copy);
  return author;
}

/**
 * Parse the package version from the given `slug`
 */

char *
clib_package_parse_version(const char *slug) {
  if (!slug) return NULL;

  char *version = strstr(slug, "@");
  if (!version) return CLIB_PACKAGE_DEFAULT_VERSION;

  version++;
  return 0 == strcmp("*", version)
    ? CLIB_PACKAGE_DEFAULT_VERSION
    : version;
}

/**
 * Parse the package name from the given `slug`
 */

char *
clib_package_parse_name(const char *slug) {
  char *copy;
  if (!slug || !(copy = strdup(slug))) return NULL;

  char *version = strstr(copy, "@");
  if (version) {
    // remove version from slug
    copy = substr(copy, 0, version - copy);
  }

  char *name = strstr(copy, "/");
  if (!name) {
    // missing author (name@version or just name)
    return copy;
  }

  // missing name (author/@version)
  name++;
  if (0 == strlen(name)) {
    free(copy);
    return NULL;
  }

  return name;
}

/**
 * Free a clib package
 */

void
clib_package_free(clib_package_t *pkg) {
  if (pkg->src) list_destroy(pkg->src);
  if (pkg->dependencies) list_destroy(pkg->dependencies);
  free(pkg);
}

/**
 * Create a new package dependency from the given `repo` and `version`
 */

clib_package_dependency_t *
clib_package_dependency_new(const char *repo, const char *version) {
  if (!repo || !version) return NULL;

  clib_package_dependency_t *dep = malloc(sizeof(clib_package_dependency_t));
  if (!dep) {
    return NULL;
  }

  dep->version = 0 == strcmp("*", version)
    ? CLIB_PACKAGE_DEFAULT_VERSION
    : strdup(version);
  dep->name = clib_package_parse_name(repo);
  dep->author = clib_package_parse_author(repo);
  dep->next = NULL;

  return dep;
}

/**
 * Install the given `pkg` in `dir`
 */

int
clib_package_install(clib_package_t *pkg, const char *dir) {
  if (!pkg || !dir) return -1;

  char *pkg_dir = path_join(dir, pkg->name);
  if (!pkg_dir) {
    return -1;
  }

  char *base_url = clib_package_url(pkg->author, pkg->repo_name, pkg->version);
  if (!base_url) {
    free(pkg_dir);
    return -1;
  }

  if (-1 == mkdirp(pkg_dir, 0777)) {
    free(pkg_dir);
    free(base_url);
    return -1;
  }

  // write package.json

  char *package_json = path_join(pkg_dir, "package.json");
  if (!package_json) return -1;
  fs_write(package_json, pkg->json);
  free(package_json);

  // write each source

  list_node_t *node;
  list_iterator_t *it = list_iterator_new(pkg->src, LIST_HEAD);
  while ((node = list_iterator_next(it))) {
    char *filename = node->val;

    // download source file

    char *file_url = clib_package_file_url(base_url, filename);
    char *file_path = path_join(pkg_dir, basename(filename));

    if (!file_url || !file_path) {
      if (file_url) free(file_url);
      return -1;
    }

    int rc = http_get_file(file_url, file_path);
    free(file_url);
    free(file_path);
    if (-1 == rc) {
      return -1;
    }
  }

  list_iterator_destroy(it);

  free(pkg_dir);
  free(base_url);

  return clib_package_install_dependencies(pkg, dir);
}

/**
 * Install the given `pkg`'s dependencies in `dir`
 */

int
clib_package_install_dependencies(clib_package_t *pkg, const char *dir) {
  if (!pkg || !dir) return -1;
  if (NULL == pkg->dependencies) return 0;

  list_node_t *node;
  list_iterator_t *it = list_iterator_new(pkg->dependencies, LIST_HEAD);
  while ((node = list_iterator_next(it))) {
    clib_package_dependency_t *dep = node->val;
    char *slug = clib_package_slug(dep->author, dep->name, dep->version);
    clib_package_t *pkg = clib_package_new_from_slug(slug);
    free(slug);
    if (NULL == pkg) {
      return -1;
    }

    clib_package_install(pkg, dir);
  }

  list_iterator_destroy(it);
  return 0;
}

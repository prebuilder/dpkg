/*
 * dpkg - main program for package management
 * verify.c - verify package integrity
 *
 * Copyright © 2012-2015 Guillem Jover <guillem@debian.org>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <compat.h>

#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <dpkg/i18n.h>
#include <dpkg/dpkg.h>
#include <dpkg/dpkg-db.h>
#include <dpkg/options.h>
#include <dpkg/db-ctrl.h>
#include <dpkg/db-fsys.h>

#include "main.h"


enum verify_result {
	VERIFY_NONE,
	VERIFY_PASS,
	VERIFY_FAIL,
};

struct verify_checks {
	enum verify_result md5sum;
};

typedef void verify_output_func(struct fsys_namenode *, struct verify_checks *);

static int
verify_result_rpm(enum verify_result result, int check)
{
	switch (result) {
	case VERIFY_FAIL:
		return check;
	case VERIFY_PASS:
		return '.';
	case VERIFY_NONE:
	default:
		return '?';
	}
}

static void
verify_output_rpm(struct fsys_namenode *namenode, struct verify_checks *checks)
{
	char result[9];
	int attr;

	memset(result, '?', sizeof(result));

	result[2] = verify_result_rpm(checks->md5sum, '5');

	if (namenode->flags & FNNF_OLD_CONFF)
		attr = 'c';
	else
		attr = ' ';

	printf("%.9s %c %s\n", result, attr, namenode->name);
}

static verify_output_func *verify_output = verify_output_rpm;

bool
verify_set_output(const char *name)
{
	if (strcmp(name, "rpm") == 0)
		verify_output = verify_output_rpm;
	else
		return false;

	return true;
}

static void
verify_package(struct pkginfo *pkg)
{
	struct fsys_namenode_list *file;
	struct varbuf filename = VARBUF_INIT;

	ensure_packagefiles_available(pkg);
	parse_filehash(pkg, &pkg->installed);
	pkg_conffiles_mark_old(pkg);

	for (file = pkg->files; file; file = file->next) {
		struct verify_checks checks;
		struct fsys_namenode *fnn;
		char hash[MD5HASHLEN + 1];
		int failures = 0;

		fnn = namenodetouse(file->namenode, pkg, &pkg->installed);

		if (fnn->newhash == NULL) {
			if (fnn->oldhash == NULL)
				continue;
			else
				fnn->newhash = fnn->oldhash;
		}

		varbuf_reset(&filename);
		varbuf_add_str(&filename, instdir);
		varbuf_add_str(&filename, fnn->name);
		varbuf_end_str(&filename);

		memset(&checks, 0, sizeof(checks));

		md5hash(pkg, hash, filename.buf);
		if (strcmp(hash, fnn->newhash) != 0) {
			checks.md5sum = VERIFY_FAIL;
			failures++;
		}

		if (failures)
			verify_output(fnn, &checks);
	}

	varbuf_destroy(&filename);
}

int
verify(const char *const *argv)
{
	struct pkginfo *pkg;
	int rc = 0;

	modstatdb_open(msdbrw_readonly, NULL);
	ensure_diversions();

	if (!*argv) {
		struct pkg_hash_iter *iter;

		iter = pkg_hash_iter_new();
		while ((pkg = pkg_hash_iter_next_pkg(iter)))
			verify_package(pkg);
		pkg_hash_iter_free(iter);
	} else {
		const char *thisarg;

		while ((thisarg = *argv++)) {
			pkg = dpkg_options_parse_pkgname(cipaction, thisarg);
			if (pkg->status == PKG_STAT_NOTINSTALLED) {
				notice(_("package '%s' is not installed"),
				       pkg_name(pkg, pnaw_nonambig));
				rc = 1;
				continue;
			}

			verify_package(pkg);
		}
	}

	modstatdb_shutdown();

	m_output(stdout, _("<standard output>"));

	return rc;
}

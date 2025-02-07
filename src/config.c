/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>

#include "ini.h"
#include "keys.h"
#include "layer.h"
#include "error.h"
#include "config.h"

#define MAX_FILE_SZ 65536
#define MAX_LINE_LEN 256

static const char *resolve_include_path(const char *path, const char *include_path)
{
	static char resolved_path[PATH_MAX];
	char tmp[PATH_MAX];
	const char *dir;

	if (strstr(include_path, "."))
		return NULL;

	strcpy(tmp, path);
	dir = dirname(tmp);
	snprintf(resolved_path, sizeof resolved_path, "%s/%s", dir, include_path);

	if (!access(resolved_path, F_OK))
		return resolved_path;

	snprintf(resolved_path, sizeof resolved_path, "/usr/share/keyd/%s", include_path);

	if (!access(resolved_path, F_OK))
		return resolved_path;

	return NULL;
}

static char *read_file(const char *path)
{
	const char include_prefix[] = "include ";

	static char buf[MAX_FILE_SZ];
	char line[MAX_LINE_LEN];
	int sz = 0;

	FILE *fh = fopen(path, "r");
	if (!fh) {
		fprintf(stderr, "\tERROR: Failed to open %s\n", path);
		return NULL;
	}

	while (fgets(line, sizeof line, fh)) {
		int len = strlen(line);

		if (line[len-1] != '\n') {
			fprintf(stderr, "\tERROR: Maximum line length exceed (%d).\n", MAX_LINE_LEN);
			goto fail;
		}

		if ((len+sz) > MAX_FILE_SZ) {
			fprintf(stderr, "\tERROR: Max file size (%d) exceeded.\n", MAX_FILE_SZ);
			goto fail;
		}

		if (strstr(line, include_prefix) == line) {
			int fd;
			const char *resolved_path;
			char *include_path = line+sizeof(include_prefix)-1;

			line[len-1] = 0;

			resolved_path = resolve_include_path(path, include_path);

			if (!resolved_path) {
				fprintf(stderr, "\tERROR: Failed to resolve include path: %s\n", include_path);
				continue;
			}

			dbg("Including %s from %s", resolved_path, path);

			fd = open(resolved_path, O_RDONLY);

			if (fd < 0) {
				fprintf(stderr, "\tERROR: Failed to include %s\n", include_path);
				perror("open");
			} else {
				int n;
				while ((n = read(fd, buf+sz, sizeof(buf)-sz)) > 0)
					sz += n;
				close(fd);
			}
		} else {
			strcpy(buf+sz, line);
			sz += len;
		}
	}

	fclose(fh);
	return buf;

fail:
	fclose(fh);
	return NULL;
}


/* Return up to two keycodes associated with the given name. */
static uint8_t lookup_keycode(const char *name)
{
	size_t i;

	for (i = 0; i < 256; i++) {
		const struct keycode_table_ent *ent = &keycode_table[i];

		if (ent->name &&
		    (!strcmp(ent->name, name) ||
		     (ent->alt_name && !strcmp(ent->alt_name, name)))) {
			return i;
		}
	}

	return 0;
}

int config_get_layer_index(const struct config *config, const char *name)
{
	size_t i;

	for (i = 0; i < config->nr_layers; i++)
		if (!strcmp(config->layers[i].name, name))
			return i;

	return -1;
}

/*
 * Consumes a string of the form `[<layer>.]<key> = <descriptor>` and adds the
 * mapping to the corresponding layer in the config.
 */

int set_layer_entry(const struct config *config, struct layer *layer,
	const char *key, const struct descriptor *d)
{
	size_t i;
	int found = 0;

	for (i = 0; i < 256; i++) {
		if (!strcmp(config->aliases[i], key)) {
			layer->keymap[i] = *d;
			found = 1;
		}
	}

	if (!found) {
		uint8_t code;

		if (!(code = lookup_keycode(key))) {
			err("%s is not a valid keycode or alias.", key);
			return -1;
		}

		layer->keymap[code] = *d;

	}

	return 0;
}

int config_add_entry(struct config *config, const char *exp)
{
	uint8_t code1, code2;
	char *keyname, *descstr, *dot, *paren, *s;
	char *layername = "main";
	struct descriptor d;
	struct layer *layer;
	int idx;

	static char buf[MAX_EXP_LEN];

	if (strlen(exp) >= MAX_EXP_LEN) {
		err("%s exceeds maximum expression length (%d)", exp, MAX_EXP_LEN);
		return -1;
	}

	strcpy(buf, exp);
	s = buf;

	dot = strchr(s, '.');
	paren = strchr(s, '(');

	if (dot && (!paren || dot < paren)) {
		layername = s;
		*dot = 0;
		s = dot+1;
	}

	parse_kvp(s, &keyname, &descstr);
	idx = config_get_layer_index(config, layername);

	if (idx == -1) {
		err("%s is not a valid layer", layername);
		return -1;
	}

	layer = &config->layers[idx];

	if (parse_descriptor(descstr, &d, config) < 0)
		return -1;

	return set_layer_entry(config, layer, keyname, &d);
}

/*
 * returns:
 * 	1 if the layer exists
 * 	0 if the layer was created successfully
 * 	< 0 on error
 */
static int config_add_layer(struct config *config, const char *s)
{
	int ret;

	char buf[MAX_LAYER_NAME_LEN];
	char *name;

	strcpy(buf, s);
	name = strtok(buf, ":");

	if (name && config_get_layer_index(config, name) != -1)
			return 1;

	if (config->nr_layers >= MAX_LAYERS) {
		err("max layers (%d) exceeded", MAX_LAYERS);
		return -1;
	}

	ret = create_layer(&config->layers[config->nr_layers], s, config);

	if (ret < 0)
		return -1;

	config->nr_layers++;
	return 0;
}

static void config_init(struct config *config)
{
	size_t i;
	struct descriptor *km;

	memset(config, 0, sizeof *config);

	config_add_layer(config, "main");

	config_add_layer(config, "control:C");
	config_add_layer(config, "meta:M");
	config_add_layer(config, "shift:S");
	config_add_layer(config, "altgr:G");
	config_add_layer(config, "alt:A");

	/* Add default modifier bindings to the main layer. */
	for (i = 0; i < MAX_MOD; i++) {
		const struct modifier_table_ent *mod = &modifier_table[i];

		struct descriptor *d1 = &config->layers[0].keymap[mod->code1];
		struct descriptor *d2 = &config->layers[0].keymap[mod->code2];

		int idx = config_get_layer_index(config, mod->name);

		assert(idx != -1);

		d1->op = OP_LAYER;
		d1->args[0].idx = idx;

		d2->op = OP_LAYER;
		d2->args[0].idx = idx;

		strcpy(config->aliases[mod->code1], mod->name);
		strcpy(config->aliases[mod->code2], mod->name);
	}

	/* In ms */
	config->macro_timeout = 600;
	config->macro_repeat_timeout = 50;

}

static void parse_globals(const char *path, struct config *config, struct ini_section *section)
{
	size_t i;

	for (i = 0; i < section->nr_entries;i++) {
		struct ini_entry *ent = &section->entries[i];

		if (!strcmp(ent->key, "macro_timeout"))
			config->macro_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "macro_sequence_timeout"))
			config->macro_sequence_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "default_layout"))
			snprintf(config->default_layout, sizeof config->default_layout,
				 "%s", ent->val);
		else if (!strcmp(ent->key, "macro_repeat_timeout"))
			config->macro_repeat_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "layer_indicator"))
			config->layer_indicator = atoi(ent->val);
		else
			fprintf(stderr, "\tERROR %s:%zd: %s is not a valid global option.\n",
					path,
					ent->lnum,
					ent->key);
	}
}

static void parse_aliases(const char *path, struct config *config, struct ini_section *section)
{
	size_t i;

	for (i = 0; i < section->nr_entries; i++) {
		uint8_t code;
		struct ini_entry *ent = &section->entries[i];
		const char *name = ent->val;

		if ((code = lookup_keycode(ent->key))) {
			ssize_t len = strlen(name);

			if (len > MAX_ALIAS_LEN) {
				fprintf(stderr,
					"\tERROR: %s exceeds the maximum alias length (%d)\n",
					name, MAX_ALIAS_LEN);
			} else {
				uint8_t alias_code;

				if ((alias_code = lookup_keycode(name))) {
					struct descriptor *d = &config->layers[0].keymap[code];

					d->op = OP_KEYSEQUENCE;
					d->args[0].code = alias_code;
					d->args[1].mods = 0;
				}

				strcpy(config->aliases[code], name);
			}
		} else {
			fprintf(stderr,
				"\tERROR %s:%zd: Failed to define alias %s, %s is not a valid keycode\n",
				path, ent->lnum, ent->key, name);
		}
	}
}

int config_parse(struct config *config, const char *path)
{
	size_t i;

	char *content;
	struct ini *ini;
	struct ini_section *section;

	config_init(config);

	if (!(content = read_file(path)))
		return -1;

	if (!(ini = ini_parse_string(content, NULL)))
		return -1;

	/* First pass: create all layers based on section headers.  */
	for (i = 0; i < ini->nr_sections; i++) {
		section = &ini->sections[i];

		if (!strcmp(section->name, "ids")) {
			;;
		} else if (!strcmp(section->name, "aliases")) {
			parse_aliases(path, config, section);
		} else if (!strcmp(section->name, "global")) {
			parse_globals(path, config, section);
		} else {
			if (config_add_layer(config, section->name) < 0)
				fprintf(stderr, "\tERROR %s:%zd: %s\n", path, section->lnum, errstr);
		}
	}

	/* Populate each layer. */
	for (i = 0; i < ini->nr_sections; i++) {
		size_t j;
		char *layername;
		section = &ini->sections[i];

		if (!strcmp(section->name, "ids") ||
		    !strcmp(section->name, "aliases") ||
		    !strcmp(section->name, "global"))
			continue;

		layername = strtok(section->name, ":");

		for (j = 0; j < section->nr_entries;j++) {
			char entry[MAX_EXP_LEN];
			struct ini_entry *ent = &section->entries[j];

			if (!ent->val) {
				fprintf(stderr, "\tERROR parsing %s:%zd: invalid key value pair.\n", path, ent->lnum);
				continue;
			}

			snprintf(entry, sizeof entry, "%s.%s = %s", layername, ent->key, ent->val);

			if (config_add_entry(config, entry) < 0)
				fprintf(stderr, "\tERROR parsing %s:%zd: %s\n", path, ent->lnum, errstr);
		}
	}

	return 0;
}

/*
 * Returns 1 in the case of a match and 2
 * in the case of an exact match.
 */
static int config_check_match(const char *path, uint16_t vendor, uint16_t product)
{
	char line[32];
	size_t line_sz = 0;

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 0;
	}

	int seen_ids = 0;
	int wildcard = 0;

	while (1) {
		char buf[1024];
		int i;
		int n = read(fd, buf, sizeof buf);

		if (!n)
			break;

		for (i = 0; i < n; i++) {
			switch (buf[i]) {
				case ' ':
					break;
				case '#':
					while ((i < n) && (buf[i] != '\n'))
						i++;
					break;
				case '[':
					if (seen_ids)
						goto end;
					else if (line_sz < sizeof(line)-1)
						line[line_sz++] = buf[i];

					break;
				case '\n':
					line[line_sz] = 0;

					if (!seen_ids && strstr(line, "[ids]") == line) {
						seen_ids++;
					} else if (seen_ids && line_sz) {
						if (line[0] == '*' && line[1] == 0) {
							wildcard = 1;
						} else {
							char *id = line;
							uint8_t omit = 0;
							uint16_t p, v;
							int ret;

							if (line[0] == '-') {
								omit = 1;
								id++;
							}

							if (line[0] != '#') {
								ret = sscanf(id, "%hx:%hx", &v, &p);

								if (ret == 2 && v == vendor && p == product) {
									close(fd);
									return omit ? 0 : 2;
								}
							}
						}
					}

					line_sz = 0;
					break;
				default:
					if (line_sz < sizeof(line)-1)
						line[line_sz++] = buf[i];

					break;
			}
		}
	}
end:

	close(fd);
	return wildcard;
}

/*
 * Scan a directory for the most appropriate match for a given vendor/product
 * pair and return the result. returns NULL if not match is found.
 */
const char *find_config_path(const char *dir, uint16_t vendor, uint16_t product, uint8_t *is_exact_match)
{
	static char result[1024];
	DIR *dh = opendir(dir);
	struct dirent *ent;
	int priority = 0;

	if (!dh) {
		perror("opendir");
		return NULL;
	}

	while ((ent = readdir(dh))) {
		char path[1024];
		int len;

		if (ent->d_type == DT_DIR)
			continue;

		len = snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
		if (len >= 5 && !strcmp(path+len-5, ".conf")) {
			int ret = config_check_match(path, vendor, product);
			if (ret && ret > priority) {
				priority = ret;
				strcpy(result, path);
			}
		}
	}

	closedir(dh);

	*is_exact_match = priority == 2;
	return priority ? result : NULL;
}

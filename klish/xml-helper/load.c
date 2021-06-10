/** @file load.c
 * @brief Common part for XML parsing.
 *
 * Different XML parsing engines can provide a functions in a form of
 * standardized API. This code uses this API and parses XML to kscheme.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>

#include <faux/faux.h>
#include <faux/str.h>
#include <faux/error.h>
#include <klish/kscheme.h>
#include <klish/ischeme.h>
#include <klish/kxml.h>

#define TAG "XML"

#ifdef DEBUG
#define KXML_DEBUG DEBUG
#endif


typedef bool_t (kxml_process_fn)(const kxml_node_t *element,
	void *parent, faux_error_t *error);

static kxml_process_fn
	process_action,
	process_param,
	process_command,
	process_view,
	process_ptype,
	process_plugin,
	process_klish;

// Different TAGs types
typedef enum {
	KTAG_NONE,
	KTAG_ACTION,
	KTAG_PARAM,
	KTAG_COMMAND,
	KTAG_VIEW,
	KTAG_PTYPE,
	KTAG_PLUGIN,
	KTAG_KLISH,
	KTAG_MAX
} ktags_e;

static const char * const kxml_tags[] = {
	NULL,
	"ACTION",
	"PARAM",
	"COMMAND",
	"VIEW",
	"PTYPE",
	"PLUGIN",
	"KLISH"
};

static kxml_process_fn *kxml_handlers[] = {
	NULL,
	process_action,
	process_param,
	process_command,
	process_view,
	process_ptype,
	process_plugin,
	process_klish
};


static const char *kxml_tag_name(ktags_e tag)
{
	if ((KTAG_NONE == tag) || (tag >= KTAG_MAX))
		return "NONE";

	return kxml_tags[tag];
}


static ktags_e kxml_node_tag(const kxml_node_t *node)
{
	ktags_e tag = KTAG_NONE;
	char *name = NULL;

	if (!node)
		return KTAG_NONE;

	if (kxml_node_type(node) != KXML_NODE_ELM)
		return KTAG_NONE;
	name = kxml_node_name(node);
	if (!name)
		return KTAG_NONE; // Strange case
	for (tag = (KTAG_NONE + 1); tag < KTAG_MAX; tag++) {
		if (faux_str_casecmp(name, kxml_tags[tag]) == 0)
			break;
	}
	kxml_node_name_free(name);
	if (tag >= KTAG_MAX)
		return KTAG_NONE;

	return tag;
}


static kxml_process_fn *kxml_node_handler(const kxml_node_t *node)
{
	return kxml_handlers[kxml_node_tag(node)];
}


/** @brief Reads an element from the XML stream and processes it.
 */
static bool_t process_node(const kxml_node_t *node, void *parent, faux_error_t *error)
{
	kxml_process_fn *handler = NULL;

	// Process only KXML_NODE_ELM. Don't process other types like:
	// KXML_NODE_DOC,
	// KXML_NODE_TEXT,
	// KXML_NODE_ATTR,
	// KXML_NODE_COMMENT,
	// KXML_NODE_PI,
	// KXML_NODE_DECL,
	// KXML_NODE_UNKNOWN,
	if (kxml_node_type(node) != KXML_NODE_ELM)
		return BOOL_TRUE;

	handler = kxml_node_handler(node);

	if (!handler) { // Unknown element
		faux_error_sprintf(error,
			TAG": Unknown tag \"%s\"", kxml_node_name(node));
		return BOOL_FALSE;
	}

#ifdef KXML_DEBUG
	printf("kxml: Tag \"%s\"\n", kxml_node_name(node));
#endif

	return handler(node, parent, error);
}


static bool_t kxml_load_file(kscheme_t *scheme, const char *filename,
	faux_error_t *error)
{
	kxml_doc_t *doc = NULL;
	kxml_node_t *root = NULL;
	bool_t r = BOOL_FALSE;

	if (!scheme)
		return BOOL_FALSE;
	if (!filename)
		return BOOL_FALSE;

#ifdef KXML_DEBUG
	printf("kxml: Processing XML file \"%s\"\n", filename);
#endif

	doc = kxml_doc_read(filename);
	if (!kxml_doc_is_valid(doc)) {
/*		int errcaps = kxml_doc_error_caps(doc);
		printf("Unable to open file '%s'", filename);
		if ((errcaps & kxml_ERR_LINE) == kxml_ERR_LINE)
			printf(", at line %d", kxml_doc_err_line(doc));
		if ((errcaps & kxml_ERR_COL) == kxml_ERR_COL)
			printf(", at column %d", kxml_doc_err_col(doc));
		if ((errcaps & kxml_ERR_DESC) == kxml_ERR_DESC)
			printf(", message is %s", kxml_doc_err_msg(doc));
		printf("\n");
*/		kxml_doc_release(doc);
		return BOOL_FALSE;
	}
	root = kxml_doc_root(doc);
	r = process_node(root, scheme, error);
	kxml_doc_release(doc);
	if (!r) {
		faux_error_sprintf(error, TAG": Illegal file %s", filename);
		return BOOL_FALSE;
	}

	return BOOL_TRUE;
}


/** @brief Default path to get XML files from.
 */
static const char *default_path = "/etc/klish;~/.klish";
static const char *path_separators = ":;";


bool_t kxml_load_scheme(kscheme_t *scheme, const char *xml_path,
	faux_error_t *error)
{
	char *path = NULL;
	char *fn = NULL;
	char *saveptr = NULL;
	bool_t ret = BOOL_TRUE;

	assert(scheme);
	if (!scheme)
		return BOOL_FALSE;

	// Use the default path if xml path is not specified.
	// Dup is needed because sring will be tokenized but
	// the xml_path is must be const.
	if (!xml_path)
		path = faux_str_dup(default_path);
	else
		path = faux_str_dup(xml_path);

#ifdef KXML_DEBUG
	printf("kxml: Loading scheme \"%s\"\n", path);
#endif

	// Loop through each directory
	for (fn = strtok_r(path, path_separators, &saveptr);
		fn; fn = strtok_r(NULL, path_separators, &saveptr)) {
		DIR *dir = NULL;
		struct dirent *entry = NULL;
		char *realpath = NULL;

		// Expand tilde. Tilde must be the first symbol.
		realpath = faux_expand_tilde(fn);

		// Regular file
		if (faux_isfile(realpath)) {
			if (!kxml_load_file(scheme, realpath, error))
				ret = BOOL_FALSE;
			faux_str_free(realpath);
			continue;
		}

		// Search this directory for any XML files
#ifdef KXML_DEBUG
		printf("kxml: Processing XML dir \"%s\"\n", realpath);
#endif
		dir = opendir(realpath);
		if (!dir) {
			faux_str_free(realpath);
			continue;
		}
		for (entry = readdir(dir); entry; entry = readdir(dir)) {
			const char *extension = strrchr(entry->d_name, '.');
			char *filename = NULL;

			// Check the filename
			if (!extension || strcmp(".xml", extension))
				continue;
			filename = faux_str_sprintf("%s/%s", realpath, entry->d_name);
			if (!kxml_load_file(scheme, filename, error))
				ret = BOOL_FALSE;
			faux_str_free(filename);
		}
		closedir(dir);
		faux_str_free(realpath);
	}

	faux_str_free(path);

	return ret;
}


/** @brief Iterate through element's children.
 */
static bool_t process_children(const kxml_node_t *element, void *parent,
	faux_error_t *error)
{
	const kxml_node_t *node = NULL;

	while ((node = kxml_node_next_child(element, node)) != NULL) {
		bool_t res = BOOL_FALSE;
		res = process_node(node, parent, error);
		if (!res)
			return res;
	}

	return BOOL_TRUE;
}


static bool_t process_klish(const kxml_node_t *element, void *parent,
	faux_error_t *error)
{
	return process_children(element, parent, error);
}


static bool_t process_view(const kxml_node_t *element, void *parent,
	faux_error_t *error)
{
	iview_t iview = {};
	kview_t *view = NULL;
	bool_t res = BOOL_FALSE;
	ktags_e parent_tag = kxml_node_tag(kxml_node_parent(element));
	kscheme_t *scheme = (kscheme_t *)parent;

	// Parent must be a KLISH tag
	if (parent_tag != KTAG_KLISH) {
		faux_error_sprintf(error,
			TAG": Tag \"%s\" can't contain VIEW tag",
			kxml_tag_name(parent_tag));
		return BOOL_FALSE;
	}
	if (!scheme) {
		faux_error_sprintf(error,
			TAG": Broken parent object for VIEW \"%s\"",
			iview.name);
		return BOOL_FALSE;
	}

	// Mandatory VIEW name
	iview.name = kxml_node_attr(element, "name");
	if (!iview.name) {
		faux_error_sprintf(error, TAG": VIEW without name");
		return BOOL_FALSE;
	}

	// Does VIEW already exist
	view = kscheme_find_view(scheme, iview.name);
	if (view) {
		if (!iview_parse(&iview, view, error))
			goto err;
	} else { // New VIEW object
		view = iview_load(&iview, error);
		if (!view)
			goto err;
		if (!kscheme_add_view(scheme, view)) {
			faux_error_sprintf(error, TAG": Can't add VIEW \"%s\". "
				"Probably duplication",
				kview_name(view));
			kview_free(view);
			goto err;
		}
	}

	if (!process_children(element, view, error))
		goto err;

	res = BOOL_TRUE;
err:
	kxml_node_attr_free(iview.name);

	return res;
}


static bool_t process_ptype(const kxml_node_t *element, void *parent,
	faux_error_t *error)
{
	iptype_t iptype = {};
	kptype_t *ptype = NULL;
	bool_t res = BOOL_FALSE;
	ktags_e parent_tag = kxml_node_tag(kxml_node_parent(element));

	if (parent_tag != KTAG_KLISH) {
		faux_error_sprintf(error,
			TAG": Tag \"%s\" can't contain PTYPE tag",
			kxml_tag_name(parent_tag));
		return BOOL_FALSE;
	}

	iptype.name = kxml_node_attr(element, "name");
	iptype.help = kxml_node_attr(element, "help");

	ptype = iptype_load(&iptype, error);
	if (!ptype)
		goto err;

	if (!kscheme_add_ptype((kscheme_t *)parent, ptype)) {
		faux_error_sprintf(error, TAG": Can't add PTYPE \"%s\". "
			"Probably duplication",
			kptype_name(ptype));
		kptype_free(ptype);
		goto err;
	}

	if (!process_children(element, ptype, error))
		goto err;

	res = BOOL_TRUE;
err:
	kxml_node_attr_free(iptype.name);
	kxml_node_attr_free(iptype.help);

	return res;
}


static bool_t process_plugin(const kxml_node_t *element, void *parent,
	faux_error_t *error)
{
	iplugin_t iplugin = {};
	kplugin_t *plugin = NULL;
	bool_t res = BOOL_FALSE;
	ktags_e parent_tag = kxml_node_tag(kxml_node_parent(element));

	if (parent_tag != KTAG_KLISH) {
		faux_error_sprintf(error,
			TAG": Tag \"%s\" can't contain PLUGIN tag",
			kxml_tag_name(parent_tag));
		return BOOL_FALSE;
	}

	iplugin.name = kxml_node_attr(element, "name");
	iplugin.id = kxml_node_attr(element, "id");
	iplugin.file = kxml_node_attr(element, "file");
	iplugin.conf = kxml_node_content(element);

	plugin = iplugin_load(&iplugin, error);
	if (!plugin)
		goto err;

	if (!kscheme_add_plugin((kscheme_t *)parent, plugin)) {
		faux_error_sprintf(error, TAG": Can't add PLUGIN \"%s\". "
			"Probably duplication",
			kplugin_name(plugin));
		kplugin_free(plugin);
		goto err;
	}

	if (!process_children(element, plugin, error))
		goto err;

	res = BOOL_TRUE;
err:
	kxml_node_attr_free(iplugin.name);
	kxml_node_attr_free(iplugin.id);
	kxml_node_attr_free(iplugin.file);
	kxml_node_content_free(iplugin.conf);

	return res;
}


static bool_t process_param(const kxml_node_t *element, void *parent,
	faux_error_t *error)
{
	iparam_t iparam = {};
	kparam_t *param = NULL;
	bool_t res = BOOL_FALSE;
	ktags_e parent_tag = kxml_node_tag(kxml_node_parent(element));

	iparam.name = kxml_node_attr(element, "name");
	iparam.help = kxml_node_attr(element, "help");
	iparam.ptype = kxml_node_attr(element, "ptype");

	param = iparam_load(&iparam, error);
	if (!param)
		goto err;

	if (KTAG_COMMAND == parent_tag) {
		kcommand_t *command = (kcommand_t *)parent;
		if (!kcommand_add_param(command, param)) {
			faux_error_sprintf(error,
				TAG": Can't add PARAM \"%s\" to COMMAND \"%s\". "
				"Probably duplication",
				kparam_name(param), kcommand_name(command));
			kparam_free(param);
			goto err;
		}
	} else if (KTAG_PARAM == parent_tag) {
		kparam_t *parent_param = (kparam_t *)parent;
		if (!kparam_add_param(parent_param, param)) {
			faux_error_sprintf(error,
				TAG": Can't add PARAM \"%s\" to PARAM \"%s\". "
				"Probably duplication",
				kparam_name(param), kparam_name(parent_param));
			kparam_free(param);
			goto err;
		}
	} else {
		faux_error_sprintf(error,
			TAG": Tag \"%s\" can't contain PARAM tag",
			kxml_tag_name(parent_tag));
		kparam_free(param);
		goto err;
	}

	if (!process_children(element, param, error))
		goto err;

	res = BOOL_TRUE;
err:
	kxml_node_attr_free(iparam.name);
	kxml_node_attr_free(iparam.help);
	kxml_node_attr_free(iparam.ptype);

	return res;
}


static bool_t process_command(const kxml_node_t *element, void *parent,
	faux_error_t *error)
{
	icommand_t icommand = {};
	kcommand_t *command = NULL;
	bool_t res = BOOL_FALSE;
	ktags_e parent_tag = kxml_node_tag(kxml_node_parent(element));

	if (parent_tag != KTAG_VIEW) {
		faux_error_sprintf(error,
			TAG": Tag \"%s\" can't contain COMMAND tag",
			kxml_tag_name(parent_tag));
		return BOOL_FALSE;
	}

	icommand.name = kxml_node_attr(element, "name");
	icommand.help = kxml_node_attr(element, "help");

	command = icommand_load(&icommand, error);
	if (!command)
		goto err;

	if (!kview_add_command((kview_t *)parent, command)) {
		faux_error_sprintf(error, TAG": Can't add COMMAND \"%s\". "
			"Probably duplication",
			kcommand_name(command));
		kcommand_free(command);
		goto err;
	}

	if (!process_children(element, command, error))
		goto err;

	res = BOOL_TRUE;
err:
	kxml_node_attr_free(icommand.name);
	kxml_node_attr_free(icommand.help);

	return res;
}


static bool_t process_action(const kxml_node_t *element, void *parent,
	faux_error_t *error)
{
	iaction_t iaction = {};
	kaction_t *action = NULL;
	bool_t res = BOOL_FALSE;
	ktags_e parent_tag = kxml_node_tag(kxml_node_parent(element));

	iaction.sym = kxml_node_attr(element, "sym");
	iaction.lock = kxml_node_attr(element, "lock");
	iaction.interrupt = kxml_node_attr(element, "interrupt");
	iaction.interactive = kxml_node_attr(element, "interactive");
	iaction.exec_on = kxml_node_attr(element, "exec_on");
	iaction.update_retcode = kxml_node_attr(element, "update_retcode");
	iaction.script = kxml_node_content(element);

	action = iaction_load(&iaction, error);
	if (!action)
		goto err;

	if (KTAG_COMMAND == parent_tag) {
		kcommand_t *command = (kcommand_t *)parent;
		if (!kcommand_add_action(command, action)) {
			faux_error_sprintf(error,
				TAG": Can't add ACTION #%d to COMMAND \"%s\". "
				"Probably duplication",
				kcommand_actions_len(command) + 1,
				kcommand_name(command));
			kaction_free(action);
			goto err;
		}
	} else if (KTAG_PTYPE == parent_tag) {
		kptype_t *ptype = (kptype_t *)parent;
		if (!kptype_add_action(ptype, action)) {
			faux_error_sprintf(error,
				TAG": Can't add ACTION #%d to PTYPE \"%s\". "
				"Probably duplication",
				kptype_actions_len(ptype) + 1,
				kptype_name(ptype));
			kaction_free(action);
			goto err;
		}
	} else {
		faux_error_sprintf(error,
			TAG": Tag \"%s\" can't contain ACTION tag",
			kxml_tag_name(parent_tag));
		kaction_free(action);
		goto err;
	}

	if (!process_children(element, action, error))
		goto err;

	res = BOOL_TRUE;
err:
	kxml_node_attr_free(iaction.sym);
	kxml_node_attr_free(iaction.lock);
	kxml_node_attr_free(iaction.interrupt);
	kxml_node_attr_free(iaction.interactive);
	kxml_node_attr_free(iaction.exec_on);
	kxml_node_attr_free(iaction.update_retcode);
	kxml_node_content_free(iaction.script);

	return res;
}
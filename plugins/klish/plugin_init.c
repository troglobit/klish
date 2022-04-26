/*
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include <faux/faux.h>
#include <klish/kplugin.h>
#include <klish/kcontext.h>

#include "private.h"


const uint8_t kplugin_klish_major = KPLUGIN_MAJOR;
const uint8_t kplugin_klish_minor = KPLUGIN_MINOR;


int kplugin_klish_init(kcontext_t *context)
{
	kplugin_t *plugin = NULL;
	ksym_t *sym = NULL;

	assert(context);
	plugin = kcontext_plugin(context);
	assert(plugin);

	// Misc
	kplugin_add_syms(plugin, ksym_new_ext("nop", klish_nop,
		KSYM_USERDEFINED_PERMANENT, KSYM_SYNC));
	kplugin_add_syms(plugin, ksym_new("tsym", klish_tsym));

	// Navigation
	// Navigation must be permanent (no dry-run) and sync. Because unsync
	// actions will be fork()-ed so it can't change current path.
	kplugin_add_syms(plugin, ksym_new_ext("nav", klish_nav,
		KSYM_PERMANENT, KSYM_SYNC));

	// PTYPEs
	// These PTYPEs are simple and fast so set SYNC flag
	kplugin_add_syms(plugin, ksym_new_ext("COMMAND", klish_ptype_COMMAND,
		KSYM_USERDEFINED_PERMANENT, KSYM_SYNC));
	kplugin_add_syms(plugin, ksym_new_ext("COMMAND_CASE", klish_ptype_COMMAND_CASE,
		KSYM_USERDEFINED_PERMANENT, KSYM_SYNC));

	context = context; // Happy compiler

	return 0;
}


int kplugin_klish_fini(kcontext_t *context)
{
//	fprintf(stderr, "Plugin 'klish' fini\n");
	context = context;

	return 0;
}

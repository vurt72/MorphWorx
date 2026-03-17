#include "plugin.hpp"
#include "version.h"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;

#ifndef METAMODULE
	INFO("MorphWorx Rack build: %s (v%s)", MORPHWORX_BUILD_ID, MORPHWORX_VERSION_STRING);
#endif

	p->addModel(modelTrigonomicon);
	p->addModel(modelSeptagon);
	p->addModel(modelPhaseon1);
	p->addModel(modelXenostasis);
}

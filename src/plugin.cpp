#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;

	p->addModel(modelTrigonomicon);
	p->addModel(modelSlideWyrm);
	p->addModel(modelSeptagon);
	p->addModel(modelMinimalith);
	p->addModel(modelAmenolith);
	p->addModel(modelPhaseon);
	p->addModel(modelXenostasis);
}

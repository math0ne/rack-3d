#include <rack3d/Element3D.hpp>
#include <rack3d/PlacementRenderer.hpp>


namespace rack {
namespace rack3d {


void Element3D::drawContent(const DrawArgs& args) {
	Widget::draw(args);
}


void Element3D::draw(const DrawArgs& args) {
	if (isDrawingRackTextures())
		return;
	drawContent(args);
}


void Element3D::drawLayer(const DrawArgs& args, int layer) {
	if (isDrawingRackTextures())
		return;
	Widget::drawLayer(args, layer);
}


} // namespace rack3d
} // namespace rack

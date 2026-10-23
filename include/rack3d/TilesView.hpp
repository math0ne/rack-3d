#pragma once
#include <widget/OpaqueWidget.hpp>


namespace rack {
namespace rack3d {


/** Debug view: every rack placement's texture in a grid of two rows, live. Toggle with F6.
While shown it replaces the 2D rack (which is hidden but still stepped), so module panels are drawn once per frame,
at the texture scale. Read-only for now.
*/
struct TilesView : widget::OpaqueWidget {
	~TilesView();
	void step() override;
	void draw(const DrawArgs& args) override;
};


bool isTilesViewShown();
void setTilesViewShown(bool shown);
void toggleTilesView();


} // namespace rack3d
} // namespace rack

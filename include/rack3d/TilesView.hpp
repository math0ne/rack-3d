#pragma once
#include <widget/OpaqueWidget.hpp>


namespace rack {
namespace rack3d {


/** Debug view: every rack placement's texture in a grid of two rows, live. Toggle with F6.
While shown it replaces the 2D rack (which is hidden but still stepped), so module panels are drawn once per frame,
at the texture scale.
Interactive: events over a tile are mapped to rack coordinates and handed to the RackWidget, so knobs, ports, menus
and module drags work, including dragging modules and cables from one tile to another. Cables aren't drawn yet.
A flat stand-in for Phase 4's ray-cast input.
*/
struct TilesView : widget::OpaqueWidget {
	~TilesView();
	void step() override;
	void draw(const DrawArgs& args) override;

	/** Where the placement at `index` (in Layout::placements) is drawn, in this widget's coordinates. */
	math::Rect getTileBox(int index);
	/** This widget's coordinates -> rack coordinates. Returns false if `pos` isn't over a tile. */
	bool toRackPos(math::Vec pos, math::Vec* rackPos);
	/** Rack coordinates -> this widget's coordinates. Returns false if `rackPos` isn't inside a placement. */
	bool fromRackPos(math::Vec rackPos, math::Vec* pos);

	void onHover(const HoverEvent& e) override;
	void onButton(const ButtonEvent& e) override;
	void onHoverKey(const HoverKeyEvent& e) override;
	void onHoverText(const HoverTextEvent& e) override;
	void onHoverScroll(const HoverScrollEvent& e) override;
	void onDragHover(const DragHoverEvent& e) override;
	void onPathDrop(const PathDropEvent& e) override;
};


/** The tiles view, or NULL if it was never shown. */
TilesView* getTilesView();


bool isTilesViewShown();
void setTilesViewShown(bool shown);
void toggleTilesView();


} // namespace rack3d
} // namespace rack

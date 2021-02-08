#include "../mwd.h"
#include <wlr/types/wlr_layer_shell_v1.h>

static bool LayerIsValid(mwdView *view)
{
	if (!view || MWD_LAYER_SHELL != view->type) {
		return false;
	}
	return true;
}

static struct wlr_surface *LayerGetSurface(mwdView *view)
{
	if (!LayerIsValid(view)) {
		return NULL;
	}

	return view->layer.surface->surface;
}

static void LayerDestroyView(mwdView *view)
{
	if (!LayerIsValid(view)) {
		return;
	}

	wl_list_remove(&view->link.drawOrder);
	/* Note; this is not in the userOrder list */

	view->type = MWD_UNKNOWN;
	free(view);
}

static void LayerEachSurface(mwdView *view, wlr_surface_iterator_func_t iterator, void *user_data)
{
	if (!LayerIsValid(view)) {
		return;
	}

	wlr_layer_surface_v1_for_each_surface(view->layer.surface, iterator, user_data);
}

static void LayerSetPos(mwdView *view, double top, double right, double bottom, double left)
{
	double			width;
	double			height;

	if (!LayerIsValid(view)) {
		return;
	}

	if (view->top		== top		&&
		view->right		== right	&&
		view->bottom	== bottom	&&
		view->left		== left
	) {
		return;
	}

	width			= right - left;
	height			= bottom - top;

	view->top		= top;
	view->right		= right;
	view->bottom	= bottom;
	view->left		= left;

	wlr_layer_surface_v1_configure(view->layer.surface, right - left, bottom - top);
}

void LayerGetPos(mwdView *view, double *ptop, double *pright, double *pbottom, double *pleft)
{
	if (!LayerIsValid(view)) {
		return;
	}

	if (ptop) {
		*ptop		= view->top;
	}
	if (pright) {
		*pright		= view->right;
	}
	if (pbottom) {
		*pbottom	= view->bottom;
	}
	if (pleft) {
		*pleft		= view->left;
	}
}

// TODO Add support for exclusive_zone
// TODO Add support for keyboard_interactive
// TODO Add support for margin
static void LayerAnchor(mwdView *view, mwdOutput *output)
{
	struct wlr_layer_surface_v1_state		state;
    uint32_t								outputw, outputh;
    uint32_t								desiredw, desiredh;
	double									top, right, bottom, left;

	state = view->layer.surface->current;

	switch (state.layer) {
		case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
			view->renderLayer = MWD_LAYER_BACKGROUND;
			break;

		case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
			view->renderLayer = MWD_LAYER_BOTTOM;
			break;

		case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
			view->renderLayer = MWD_LAYER_TOP;
			break;

		case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
			view->renderLayer = MWD_LAYER_OVERLAY;
			break;
	}

    outputw = output->output->width;
    outputh = output->output->height;

	desiredw = state.desired_width;
	if (desiredw == 0) {
		desiredw = outputw;
	}
	desiredh = state.desired_height;
	if (desiredh == 0) {
		desiredh = outputh;
	}

	if (state.anchor & (ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
		/* Anchored to one or both of the sides */
		if (state.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) {
			left	= 0;
		} else {
			left	= outputw - desiredw;
		}

		if (state.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) {
			right = outputw;
		} else {
			right = desiredw;
		}
	} else {
		/* Neither left or right was anchored; Use the requested width and center */
		left	= (outputw / 2) - (desiredw / 2);
		right	= (outputw / 2) + (desiredw / 2);
	}

	if (state.anchor & (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
		/* Anchored to one or both of the sides */
		if (state.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) {
			top	= 0;
		} else {
			top	= outputh - desiredh;
		}

		if (state.anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) {
			bottom = outputh;
		} else {
			bottom = desiredh;
		}
	} else {
		/* Neither top or bottom was anchored; Use the requested height and center */
		top		= (outputh / 2) - (desiredh / 2);
		bottom	= (outputh / 2) + (desiredh / 2);
	}

	LayerSetPos(view, top, right, bottom, left);
}

static bool LayerIsVisible(mwdView *view, mwdOutput *output)
{
	if (!LayerIsValid(view) || !output) {
		return false;
	}

	/* Position the view if needed */
	LayerAnchor(view, output);

	return true;
}

static bool LayerIsAt(mwdView *view, double x, double y, struct wlr_surface **psurface, double *offsetX, double *offsetY)
{
	struct wlr_surface		*surface;
	double					offX, offY;

	if (!LayerIsValid(view)) {
		return false;
	}

	/* This expects and returns surface local coordinates */
	if (!(surface = wlr_layer_surface_v1_surface_at(view->layer.surface, x - view->left, y - view->top, &offX, &offY))) {
		return false;
	}

	if (offsetX) {
		*offsetX = offX;
	}
	if (offsetY) {
		*offsetY = offY;
	}

	if (psurface) {
		*psurface = surface;
	}
	return TRUE;
}

mwdViewInterface LayerShellViewInterface = {
	.set = {
		.pos			= &LayerSetPos,
		.activated		= NULL
	},

	.get = {
		.pos			= &LayerGetPos,
		.surface		= &LayerGetSurface,
		.constraints	= NULL,
	},

	.destroy			= &LayerDestroyView,

	.is = {
		.valid			= &LayerIsValid,
		.visible		= &LayerIsVisible,
		.at				= &LayerIsAt,
	},

	.foreach = {
		.surface		= &LayerEachSurface
	}
};

/* Received a new layer surface from a client.  */
static void LayerNewSurface(struct wl_listener *listener, void *data)
{
	mwdServer						*server			= wl_container_of(listener, server, newSurface.layer);
	struct wlr_layer_surface_v1		*surface		= data;
	mwdView							*view;
	mwdOutput						*output;

	/* Allocate our own view structure for this surface */
	if (!(view = CreateNewView(server))) {
		return;
	}
	view->type			= MWD_LAYER_SHELL;
	view->layer.surface	= surface;
	view->cb			= &LayerShellViewInterface;

	/* Listen to the various events it can emit */
	wl_signal_add(&surface->events.map,			&view->map);
	wl_signal_add(&surface->events.unmap,		&view->unmap);
	wl_signal_add(&surface->events.destroy,		&view->destroy);

	/* Add it to the draw order list only. A user can't select this. */
	wl_list_insert(&server->views.drawOrder, &view->link.drawOrder);

	if ((output = OutputFind(server, view->layer.surface->output))) {
		view->layer.surface->output = output->output;
	}

	LayerAnchor(view, output);
}

void LayerMain(mwdServer *server)
{
	server->shell.layer = wlr_layer_shell_v1_create(server->display);

	server->newSurface.layer.notify = LayerNewSurface;
	wl_signal_add(&server->shell.layer->events.new_surface, &server->newSurface.layer);
}



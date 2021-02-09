#include "../mwd.h"

static bool XWaylandIsValid(mwdView *view)
{
	if (!view || MWD_XWAYLAND_SHELL != view->type) {
		return false;
	}
	return true;
}

static struct wlr_surface *XWaylandGetSurface(mwdView *view)
{
	if (!XWaylandIsValid(view)) {
		return NULL;
	}

	return view->xwayland.surface->surface;
}

static void XWaylandDestroyView(mwdView *view)
{
	if (!XWaylandIsValid(view)) {
		return;
	}

	wl_list_remove(&view->link.drawOrder);
	/* Note; this is not in the userOrder list */

	view->type = MWD_UNKNOWN;
	free(view);
}

static void XWaylandEachSurface(mwdView *view, wlr_surface_iterator_func_t iterator, void *user_data)
{
	if (!XWaylandIsValid(view)) {
		return;
	}

	if (view->xwayland.surface->surface) {
		wlr_surface_for_each_surface(view->xwayland.surface->surface, iterator, user_data);
	}
}

static void XWaylandSetPos(mwdView *view, double top, double right, double bottom, double left)
{
	if (!XWaylandIsValid(view)) {
		return;
	}

	if (view->top		== top		&&
		view->right		== right	&&
		view->bottom	== bottom	&&
		view->left		== left
	) {
		return;
	}

	view->top		= top;
	view->right		= right;
	view->bottom	= bottom;
	view->left		= left;

    wlr_xwayland_surface_configure(view->xwayland.surface, view->left, view->top, right - left, bottom - top);
}


void XWaylandGetPos(mwdView *view, double *ptop, double *pright, double *pbottom, double *pleft)
{
	double				top, right, bottom, left;

	if (!XWaylandIsValid(view)) {
		return;
	}

	if (view->edges & WLR_EDGE_TOP) {
		top = view->top;
		bottom = top + view->xwayland.surface->height;
	} else {
		bottom = view->bottom;
		top = bottom - view->xwayland.surface->height;
	}

	if (view->edges & WLR_EDGE_LEFT) {
		left = view->left;
		right = left + view->xwayland.surface->width;
	} else {
		right = view->right;
		left = right - view->xwayland.surface->width;
	}

	if (ptop) {
		*ptop		= top;
	}
	if (pright) {
		*pright		= right;
	}
	if (pbottom) {
		*pbottom	= bottom;
	}
	if (pleft) {
		*pleft		= left;
	}
}

static void XWaylandSetActivated(mwdView *view, bool activated)
{
	if (!XWaylandIsValid(view)) {
		return;
	}

	view->xwayland.activated = activated;
	wlr_xwayland_surface_activate(view->xwayland.surface, activated);
}

static bool XWaylandIsVisible(mwdView *view, mwdOutput *output)
{
	if (!XWaylandIsValid(view) || !output) {
		return false;
	}

	// TODO Check to see if we are on this output
	return true;
}

static bool XWaylandIsAt(mwdView *view, double x, double y, struct wlr_surface **psurface, double *offsetX, double *offsetY)
{
	struct wlr_surface		*surface;
	double					offX, offY;

	if (!XWaylandIsValid(view)) {
		return false;
	}

	if (!view->xwayland.surface->surface) {
		return false;
	}

	/* This expects and returns surface local coordinates */
	if (!(surface = wlr_surface_surface_at(view->xwayland.surface->surface, x - view->left, y - view->top, &offX, &offY))) {
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

static void XWaylandRenderView(mwdView *view, struct wlr_renderer *renderer, mwdOutput *output)
{
	mwdRenderData			rdata;

	if (!XWaylandIsValid(view)) {
		return;
	}

	memset(&rdata, 0, sizeof(rdata));

	rdata.output		= output->output;
	rdata.renderer		= renderer;
	rdata.view			= view;

	clock_gettime(CLOCK_MONOTONIC, &rdata.when);

    wlr_surface_for_each_surface(view->xwayland.surface->surface, RenderSurface, &rdata);
}

mwdViewInterface XWaylandViewInterface = {
	.set = {
		.pos			= &XWaylandSetPos,
		.activated		= &XWaylandSetActivated
	},

	.get = {
		.pos			= &XWaylandGetPos,
		.surface		= &XWaylandGetSurface,
		.constraints	= NULL,
	},

	.is = {
		.valid			= &XWaylandIsValid,
		.visible		= &XWaylandIsVisible,
		.at				= &XWaylandIsAt,
	},

	.foreach = {
		.surface		= &XWaylandEachSurface
	},

	.destroy			= &XWaylandDestroyView,
	.render				= &XWaylandRenderView
};

/* Received a new xwayland surface from a client.  */
static void XWaylandNewSurface(struct wl_listener *listener, void *data)
{
	mwdServer					*server			= wl_container_of(listener, server, xwayland.newSurface);
	mwdView						*view;
	struct wlr_xwayland_surface	*surface		= data;

	/* Allocate our own view structure for this surface */
	if (!(view = CreateNewView(server))) {
		return;
	}
	view->type				= MWD_XWAYLAND_SHELL;
	view->xwayland.surface	= surface;
	view->cb				= &XWaylandViewInterface;

	/* Listen to the various events it can emit */
	wl_signal_add(&surface->events.map,						&view->map);
	wl_signal_add(&surface->events.unmap,					&view->unmap);
	wl_signal_add(&surface->events.destroy,					&view->destroy);

	/* Add it to the list of views */
	wl_list_insert(&server->views.drawOrder, &view->link.drawOrder);
	wl_list_insert(&server->views.userOrder, &view->link.userOrder);
}

static void XWaylandReady(struct wl_listener *listener, void *data)
{
	mwdServer					*server			= wl_container_of(listener, server, xwayland.ready);

    wlr_xwayland_set_seat(server->xwayland.shell, server->seat);
}

// TODO view->xwayland.surface->size_hints can be used for constraints
// TODO There are many other events to listen to for xwayland

void XWaylandMain(mwdServer *server)
{
	if (!(server->xwayland.shell = wlr_xwayland_create(server->display, server->compositor, TRUE))) {
		wlr_log(WLR_ERROR, "Failed to start Xwayland");
		unsetenv("DISPLAY");
	} else {
		server->xwayland.newSurface.notify = XWaylandNewSurface;
		wl_signal_add(&server->xwayland.shell->events.new_surface, &server->xwayland.newSurface);

		server->xwayland.ready.notify = XWaylandReady;
		wl_signal_add(&server->xwayland.shell->events.ready, &server->xwayland.ready);

		setenv("DISPLAY", server->xwayland.shell->display_name, true);
	}
}



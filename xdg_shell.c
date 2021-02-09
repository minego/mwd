#include "../mwd.h"
#include <wlr/types/wlr_xdg_shell.h>

/*
	A client is asking to be resized. This usually indicates that a client side
	decoration is being used.
*/
static void XdgRequestResize(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_resize_event	*event	= data;
	struct mwdView							*view	= wl_container_of(listener, view, requestResize);

	if (!ViewIsFocused(view)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}

	ViewGrab(view, MWD_GRAB_RESIZE, event->edges);
}

static bool XdgIsValid(mwdView *view)
{
	if (!view || MWD_XDG_SHELL != view->type) {
		return false;
	}
	return true;
}

bool XdgGetConstraints(mwdView *view, double *minWidth, double *maxWidth, double *minHeight, double *maxHeight)
{
	struct wlr_xdg_toplevel_state	*state;

	if (!XdgIsValid(view)) {
		return false;
	}

	if ((state = &view->xdg.surface->toplevel->current)) {
		if (minWidth) {
			*minWidth	= state->min_width;
		}

		if (maxWidth) {
			*maxWidth	= state->max_width;
		}

		if (minHeight) {
			*minHeight	= state->min_height;
		}

		if (maxHeight) {
			*maxHeight	= state->max_height;
		}

		return true;
	}

	return false;
}

static bool XdgIsVisible(mwdView *view, mwdOutput *output)
{
	if (!XdgIsValid(view)) {
		return false;
	}

	// TODO Check to see if we are on this output
	return true;
}

static bool XdgIsAt(mwdView *view, double x, double y, struct wlr_surface **psurface, double *offsetX, double *offsetY)
{
	struct wlr_surface		*surface;
	double					offX, offY;

	if (!XdgIsValid(view)) {
		return false;
	}

	/* This expects and returns surface local coordinates */
	if (!(surface = wlr_xdg_surface_surface_at(view->xdg.surface, x - view->left, y - view->top, &offX, &offY))) {
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

static void XdgSetPos(mwdView *view, double top, double right, double bottom, double left)
{
    struct wlr_box	box;
	double			width;
	double			height;

	if (!XdgIsValid(view)) {
		return;
	}

	width			= right - left;
	height			= bottom - top;

    wlr_xdg_surface_get_geometry(view->xdg.surface, &box);

	view->top		= top		- box.y;
	view->right		= right		- box.x;
	view->bottom	= bottom	- box.y;
	view->left		= left		- box.x;

	if (box.height != height || box.width != width) {
		wlr_xdg_toplevel_set_size(view->xdg.surface, width, height);
	}
}

void XdgGetPos(mwdView *view, double *ptop, double *pright, double *pbottom, double *pleft)
{
	struct wlr_box		box;
	double				top, right, bottom, left;

	if (!XdgIsValid(view)) {
		return;
	}

    wlr_xdg_surface_get_geometry(view->xdg.surface, &box);

	if (view->edges & WLR_EDGE_TOP) {
		top		= view->top + box.y;
		bottom	= top + box.height;
	} else {
		bottom	= view->bottom + box.y;
		top		= bottom - box.height;
	}

	if (view->edges & WLR_EDGE_LEFT) {
		left	= view->left + box.x;
		right	= left + box.width;
	} else {
		right	= view->right + box.x;
		left	= right - box.width;
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

static void XdgSetActivated(mwdView *view, bool activated)
{
	if (!XdgIsValid(view)) {
		return;
	}

	view->xdg.activated = activated;
	wlr_xdg_toplevel_set_activated(view->xdg.surface, activated);
}

static bool XdgGetActivated(mwdView *view)
{
	if (!XdgIsValid(view)) {
		return false;
	}

	return view->xdg.activated;
}

static struct wlr_surface *XdgGetSurface(mwdView *view)
{
	if (!XdgIsValid(view)) {
		return NULL;
	}

	return view->xdg.surface->surface;
}

static void XdgDestroyView(mwdView *view)
{
	if (!XdgIsValid(view)) {
		return;
	}

	wl_list_remove(&view->link.drawOrder);
	wl_list_remove(&view->link.userOrder);

	view->type = MWD_UNKNOWN;
	free(view);
}

static void XdgEachSurface(mwdView *view, wlr_surface_iterator_func_t iterator, void *user_data)
{
	if (!XdgIsValid(view)) {
		return;
	}

	wlr_xdg_surface_for_each_surface(view->xdg.surface, iterator, user_data);
}

static void XdgRenderView(mwdView *view, struct wlr_renderer *renderer, mwdOutput *output)
{
	mwdRenderData			rdata;

	if (!XdgIsValid(view)) {
		return;
	}

	memset(&rdata, 0, sizeof(rdata));

	rdata.output		= output->output;
	rdata.renderer		= renderer;
	rdata.view			= view;

	clock_gettime(CLOCK_MONOTONIC, &rdata.when);

    wlr_surface_for_each_surface(view->xdg.surface->surface, RenderSurface, &rdata);
    wlr_xdg_surface_for_each_popup(view->xdg.surface, RenderPopupSurface, &rdata);
}

struct mwdViewInterface XdgShellViewInterface = {
	.set = {
		.pos			= &XdgSetPos,
		.activated		= &XdgSetActivated,
	},

	.get = {
		.surface		= &XdgGetSurface,
		.constraints	= &XdgGetConstraints,
		.pos			= &XdgGetPos,
		.activated		= &XdgGetActivated,
	},

	.is = {
		.valid			= &XdgIsValid,
		.at				= &XdgIsAt,
		.visible		= &XdgIsVisible
	},

	.foreach = {
		.surface		= &XdgEachSurface
	},

	.destroy			= &XdgDestroyView,
	.render				= &XdgRenderView
};

/*
	Received a new xdg surface from a client. This client can be either a
	toplevel (ie application window) or a popup.
*/
static void XdgNewSurface(struct wl_listener *listener, void *data)
{
	mwdServer				*server			= wl_container_of(listener, server, xdgShell.newSurface);
	mwdView					*view;
	struct wlr_xdg_surface	*surface		= data;

	if (surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	/* Allocate our own view structure for this surface */
	if (!(view = CreateNewView(server))) {
		return;
	}
	view->type			= MWD_XDG_SHELL;
	view->xdg.surface	= surface;
	view->cb			= &XdgShellViewInterface;

	/* Listen to the various events it can emit */
	view->requestResize.notify	= XdgRequestResize;

	wl_signal_add(&surface->events.map,						&view->map);
	wl_signal_add(&surface->events.unmap,					&view->unmap);
	wl_signal_add(&surface->events.destroy,					&view->destroy);

	wl_signal_add(&surface->toplevel->events.request_move,	&view->requestMove);
	wl_signal_add(&surface->toplevel->events.request_resize,&view->requestResize);


	/* Add it to the list of views */
	wl_list_insert(&server->views.drawOrder, &view->link.drawOrder);
	wl_list_insert(&server->views.userOrder, &view->link.userOrder);
}

void XdgMain(mwdServer *server)
{
	server->xdgShell.shell = wlr_xdg_shell_create(server->display);

	server->xdgShell.newSurface.notify = XdgNewSurface;
	wl_signal_add(&server->xdgShell.shell->events.new_surface, &server->xdgShell.newSurface);
}


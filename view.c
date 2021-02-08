#include "../mwd.h"

struct wlr_surface *ViewGetSurface(mwdView *view)
{
	if (!view || !view->cb || !view->cb->get.surface) {
		return NULL;
	}

	return view->cb->get.surface(view);
}

void ViewForEachSurface(mwdView *view, wlr_surface_iterator_func_t iterator, void *user_data)
{
	if (!view || !view->cb || !view->cb->foreach.surface) {
		return;
	}

	return view->cb->foreach.surface(view, iterator, user_data);
}

void ViewGetPos(mwdView *view, double *top, double *right, double *bottom, double *left)
{
	if (!view || !view->cb || !view->cb->get.pos) {
		return;
	}

	view->cb->get.pos(view, top, right, bottom, left);
}

void ViewGetSize(mwdView *view, double *width, double *height)
{
	double		top, right, bottom, left;

	top = right = bottom = left = 0;
	ViewGetPos(view, &top, &right, &bottom, &left);

	if (width) {
		*width = right - left;
	}

	if (height) {
		*height = bottom - top;
	}
}

bool ViewIsFocused(mwdView *view)
{
	if (!view || !view->cb || !view->cb->get.activated) {
		return false;
	}

	return view->cb->get.activated(view);
}

void ViewFocus(mwdView *view, bool raise)
{
	mwdServer				*server;
	struct wlr_seat			*seat;
	struct wlr_surface		*was;
	struct wlr_surface		*surface;
	struct wlr_keyboard		*keyboard;

	if (view == NULL) {
		return;
	}
	server	= view->server;
	seat	= server->seat;
	was		= seat->keyboard_state.focused_surface;

	if (raise) {
		/*
			Move the view to the front

			This has to be done even if the view is already focused, because it
			may have been focused without being raised.

			It is important to only move it to the end of the drawOrder list
			because raising it should not change its position in the user's
			window list.
		*/
		wl_list_remove(&view->link.drawOrder);
		wl_list_insert(&server->views.drawOrder, &view->link.drawOrder);
	}

	surface = ViewGetSurface(view);
	if (was) {
		if (was == surface) {
			/* It is already focused */
			return;
		}

		/* Inform the previously selected view that it is no longer focused */
		ViewSetActivated(ViewFindBySurface(server, was), false);
	}

	keyboard = wlr_seat_get_keyboard(seat);

	/* Activate the new surface */
	if (view && surface) {
		ViewSetActivated(view, true);
		wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

mwdView *ViewFocused(mwdServer *server)
{
	if (server && server->seat && server->seat->keyboard_state.focused_surface) {
		return ViewFindBySurface(server, server->seat->keyboard_state.focused_surface);
	}

	return NULL;
}

/*
	The ViewNext and ViewPrev calls are swapped because the userOrder list is
	actually backwards, since it is easier to insert at the start of a list than
	it is to append to the end.
*/
mwdView *ViewPrev(mwdView *view)
{
	mwdServer	*server;
	void		*next;

	if (!view) {
		return NULL;
	}
	server = view->server;

	next = view->link.userOrder.next;
	if (next == &server->views.userOrder) {
		/* We are on the last item, wrap */
		next = server->views.userOrder.next;
	}

	view = wl_container_of(next, view, link.userOrder);
	return view;
}

mwdView *ViewNext(mwdView *view)
{
	mwdServer	*server;
	void		*prev;

	if (!view) {
		return NULL;
	}
	server = view->server;

	prev = view->link.userOrder.prev;
	if (prev == &server->views.userOrder) {
		/* We are on the last item, wrap */
		prev = server->views.userOrder.prev;
	}

	view = wl_container_of(prev, view, link.userOrder);
	return view;
}

void ViewGrab(mwdView *view, mwdGrabMode mode, uint32_t edges)
{
	mwdServer				*server	= view->server;

	server->grab.view		= view;
	server->grab.mode		= mode;
	server->grab.edges		= edges;

	/* Get the initial position of the view */
	ViewGetPos(view, &server->grab.top, &server->grab.right, &server->grab.bottom, &server->grab.left);

	/* Grab the initial position of the cursor */
	server->grab.cursor.x	= server->cursor->x;
	server->grab.cursor.y	= server->cursor->y;
}

mwdView *ViewFindByPos(mwdServer *server, double x, double y, struct wlr_surface **psurface, double *offsetX, double *offsetY)
{
	/*
		Look through all of the views and attempt to find one under the cursor.
		This relies on server->views being top to bottom.
	*/
	mwdView					*view;
	struct wlr_surface		*surface;
	bool					found	= false;
	uint32_t				top, right, bottom, left;

	if (psurface) {
		*psurface = NULL;
	}

	wl_list_for_each(view, &server->views.drawOrder, link.drawOrder) {
		if (view->cb && view->cb->is.at &&
			view->cb->is.at(view, x, y, psurface, offsetX, offsetY)
		) {
			return view;
		}
	}
	return NULL;
}

mwdView *ViewFindBySurface(mwdServer *server, struct wlr_surface *surface)
{
	/*
		Look through all of the views and attempt to find one under the cursor.
		This relies on server->views being top to bottom.
	*/
	mwdView		*view;

	wl_list_for_each(view, &server->views.drawOrder, link.drawOrder) {
		if (surface == ViewGetSurface(view)) {
			return view;
		}
	}
	return NULL;
}

void ViewSetActivated(mwdView *view, bool activated)
{
	if (!view || !view->cb || !view->cb->set.activated) {
		return;
	}

	view->cb->set.activated(view, activated);
}

#define MIN_VIEW_SIZE		50

bool ViewGetConstraints(mwdView *view, double *minWidth, double *maxWidth, double *minHeight, double *maxHeight)
{
	bool		result;

	if (minWidth) {
		*minWidth = 0;
	}
	if (maxWidth) {
		*maxWidth = 0;
	}
	if (minHeight) {
		*minHeight = 0;
	}
	if (maxWidth) {
		*maxWidth = 0;
	}

	if (view && view->cb && view->cb->get.constraints) {
		result = view->cb->get.constraints(view, minWidth, maxWidth, minHeight, maxHeight);
	}

	/* Don't go below our own minimums either */
	if (minWidth && *minWidth < MIN_VIEW_SIZE) {
		*minWidth = MIN_VIEW_SIZE;
	}
	if (minHeight && *minHeight < MIN_VIEW_SIZE) {
		*minHeight = MIN_VIEW_SIZE;
	}

	return result;
}

bool ViewSetPos(mwdView *view, double top, double right, double bottom, double left)
{
	if (!view || !view->cb || !view->cb->set.pos) {
		return NULL;
	}

	view->cb->set.pos(view, top, right, bottom, left);
}

static void map(struct wl_listener *listener, void *data)
{
	struct mwdView *view = wl_container_of(listener, view, map);
	view->mapped = true;
	ViewFocus(view, true);
}

static void unmap(struct wl_listener *listener, void *data)
{
	struct mwdView *view = wl_container_of(listener, view, unmap);

	if (ViewIsFocused(view)) {
		ViewFocus(ViewPrev(view), true);
	}

	view->mapped = false;
}

bool ViewIsValid(mwdView *view)
{
	if (!view || !view->cb || !view->cb->is.valid) {
		return false;
	}

	return view->cb->is.valid(view);
}

bool ViewIsVisible(mwdView *view, mwdOutput *output)
{
	if (!ViewIsValid(view)) {
		return false;
	}

	if (!view->mapped) {
		return false;
	}

	if (!view || !view->cb || !view->cb->is.visible) {
		return false;
	}

	return view->cb->is.visible(view, output);
}

static void destroy(struct wl_listener *listener, void *data)
{
	struct mwdView	*view		= wl_container_of(listener, view, destroy);

	if (!view || !view->cb || !view->cb->destroy) {
		return;
	}

	view->cb->destroy(view);
}

/*
	A client is asking to be moved. This usually indicates that a client side
	decoration is being used.
*/
static void requestMove(struct wl_listener *listener, void *data)
{
	struct mwdView		*view	= wl_container_of(listener, view, requestMove);
	struct mwdServer	*server	= view->server;

	if (!ViewIsFocused(view)) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}

	ViewGrab(view, MWD_GRAB_MOVE, view->edges);
}

mwdView *CreateNewView(mwdServer *server)
{
	struct mwdView			*view;

	/* Allocate our own view structure for this surface */
	if (!(view = calloc(1, sizeof(struct mwdView)))) {
		return NULL;
	}
	view->type			= MWD_UNKNOWN;
	view->server		= server;
	view->renderLayer	= MWD_LAYER_NORMAL;

	/* Initially we are positioning this view from the top left */
	view->edges			= WLR_EDGE_TOP | WLR_EDGE_LEFT;

	/* Set the default listeners, they will be registered as needed by each shell */
	view->map.notify			= map;
	view->unmap.notify			= unmap;
	view->destroy.notify		= destroy;
	view->requestMove.notify	= requestMove;

	return view;
}


